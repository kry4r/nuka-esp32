// SPDX-License-Identifier: AGPL-3.0-or-later
#include "sim.h"
#include "SDL.h"
#include "app.h"
#include "store.h"
#include "nuka_events.h"
#include "nuka_time.h"
#include "hub_client.h"
#include "battery.h"
#include "power.h"
#include "net.h"
#include "usb.h"
#include "macro.h"
#include "media_cover.h"
#include "audio.h"
#include "overlay.h"
#include "perf_hud.h"
#include "vault.h"
#include "esp_timer.h"
#include "esp_partition.h"
#include "esp_random.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "esp_littlefs.h"
#include "mbedtls/entropy.h"
#include "mbedtls/platform_util.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

bool sim_running = true;
unsigned sim_battery_pct=80;
const char *NUKA_EVENTS = "sim";
esp_event_loop_handle_t nuka_bus;
nuka_settings_t nuka_settings = { .lang = "zh-CN", .brightness = 70, .t_dim = 20, .t_off = 10,
    .raise = true, .clock_style = true, .key3_action = "talk", .key3_double_card = "buddy",
    .cards = "clock,hosts,servers,ai,buddy,pomodoro,media,weather,vault,settings",
    .home_face = "clock", .volume = 70, .assistant = false };
static bool screen_on = true;
static bool provisioning_ready;
static uint8_t *asset_bytes;
static size_t asset_size;
static cJSON *settings;
static const esp_partition_t assets_partition = { 6u * 1024 * 1024, 0x40 }, vault_partition = { 65536, 2 };
static struct { uint8_t *bytes; size_t size; } nvs_values[3];

static void fail(const char *message) { fprintf(stderr, "%s\n", message); exit(1); }
static char *read_file(const char *path, size_t *size)
{
    FILE *file = fopen(path, "rb"); if (!file) fail("Cannot open simulator input file");
    fseek(file, 0, SEEK_END); long length = ftell(file); rewind(file);
    if (length < 0) fail("Cannot size simulator input file");
    char *bytes = malloc((size_t)length + 1);
    if (!bytes || fread(bytes, 1, length, file) != (size_t)length) fail("Cannot read simulator input file");
    bytes[length] = 0; fclose(file); *size = length; return bytes;
}
void sim_assets(const char *path) { asset_bytes = (uint8_t *)read_file(path, &asset_size); }
void sim_load_examples(const char *directory, bool minimal)
{
    for (int topic = 0; topic < VM_COUNT; ++topic) {
        char path[2048]; snprintf(path, sizeof(path), "%s/vm.%s.%s.json", directory,
                                  vm_topic_name(topic), minimal ? "min" : "full");
        size_t size; char *bytes = read_file(path, &size);
        cJSON *frame = cJSON_ParseWithLength(bytes, size);
        const cJSON *data = cJSON_GetObjectItemCaseSensitive(frame, "d");
        if (!cJSON_IsObject(data) || !cJSON_IsNumber(cJSON_GetObjectItemCaseSensitive(data, "rev"))) fail("Invalid VM example input");
        vm_store_replace(topic, data); cJSON_Delete(frame); free(bytes);
    }
}
cJSON *sim_example(const char *directory, const char *name)
{
    char path[2048]; snprintf(path, sizeof(path), "%s/%s.json", directory, name);
    size_t size; char *bytes = read_file(path, &size);
    cJSON *frame = cJSON_ParseWithLength(bytes, size); free(bytes);
    cJSON *data = cJSON_DetachItemFromObjectCaseSensitive(frame, "d");
    if (!cJSON_IsObject(data)) fail("Invalid example frame");
    cJSON_Delete(frame); return data;
}
int64_t esp_timer_get_time(void)
{
    uint64_t ticks = SDL_GetPerformanceCounter(), frequency = SDL_GetPerformanceFrequency();
    return (int64_t)(ticks / frequency * 1000000 + ticks % frequency * 1000000 / frequency);
}
void esp_fill_random(void *output, size_t size)
{
    mbedtls_entropy_context entropy; mbedtls_entropy_init(&entropy);
    uint8_t *at = output;
    while (size) {
        size_t part = size > 32 ? 32 : size;
        if (mbedtls_entropy_func(&entropy, at, part)) fail("Host entropy unavailable");
        at += part; size -= part;
    }
    mbedtls_entropy_free(&entropy);
}
uint32_t esp_random(void) { uint32_t value; esp_fill_random(&value, sizeof(value)); return value; }
void esp_restart(void) { sim_running = false; }
const esp_partition_t *esp_partition_find_first(unsigned type, unsigned subtype, const char *label)
{ (void)type; (void)subtype; return !strcmp(label, "assets") ? &assets_partition : &vault_partition; }
esp_err_t esp_partition_read(const esp_partition_t *partition, size_t offset, void *output, size_t size)
{
    (void)partition;
    if (offset > asset_size || size > asset_size - offset) return ESP_ERR_INVALID_SIZE;
    memcpy(output, asset_bytes + offset, size); return ESP_OK;
}
esp_err_t esp_partition_mmap(const esp_partition_t *partition, size_t offset, size_t size,
                             unsigned mode, const void **output, esp_partition_mmap_handle_t *handle)
{
    (void)partition; (void)mode;
    if (offset > asset_size || size > asset_size - offset) return ESP_ERR_INVALID_SIZE;
    *output = asset_bytes + offset; *handle = 0; return ESP_OK;
}
void esp_partition_munmap(esp_partition_mmap_handle_t handle) { (void)handle; }
static unsigned nvs_index(const char *key) { return !strcmp(key, "root") ? 0 : !strcmp(key, "hmac") ? 1 : 2; }
esp_err_t nvs_open_from_partition(const char *partition, const char *name, unsigned mode, nvs_handle_t *handle)
{ (void)partition; (void)name; (void)mode; *handle = 1; return ESP_OK; }
void nvs_close(nvs_handle_t handle) { (void)handle; }
esp_err_t nvs_get_blob(nvs_handle_t handle, const char *key, void *output, size_t *size)
{
    (void)handle; unsigned at = nvs_index(key);
    if (!nvs_values[at].bytes) return ESP_ERR_NVS_NOT_FOUND;
    if (output && *size < nvs_values[at].size) return ESP_ERR_INVALID_SIZE;
    if (output) memcpy(output, nvs_values[at].bytes, nvs_values[at].size);
    *size = nvs_values[at].size; return ESP_OK;
}
esp_err_t nvs_set_blob(nvs_handle_t handle, const char *key, const void *data, size_t size)
{
    (void)handle; if (size > vault_partition.size) return ESP_ERR_NO_MEM;
    unsigned at = nvs_index(key); uint8_t *bytes = malloc(size);
    if (!bytes) return ESP_ERR_NO_MEM;
    memcpy(bytes, data, size);
    if (nvs_values[at].bytes) mbedtls_platform_zeroize(nvs_values[at].bytes, nvs_values[at].size);
    free(nvs_values[at].bytes); nvs_values[at].bytes = bytes; nvs_values[at].size = size; return ESP_OK;
}
esp_err_t nvs_get_u8(nvs_handle_t handle, const char *key, uint8_t *value)
{ size_t size = 1; return nvs_get_blob(handle, key, value, &size); }
esp_err_t nvs_set_u8(nvs_handle_t handle, const char *key, uint8_t value)
{ return nvs_set_blob(handle, key, &value, 1); }
esp_err_t nvs_erase_key(nvs_handle_t handle, const char *key)
{
    (void)handle; unsigned at = nvs_index(key);
    if (!nvs_values[at].bytes) return ESP_ERR_NVS_NOT_FOUND;
    mbedtls_platform_zeroize(nvs_values[at].bytes, nvs_values[at].size); free(nvs_values[at].bytes);
    nvs_values[at].bytes = NULL; nvs_values[at].size = 0; return ESP_OK;
}
esp_err_t nvs_commit(nvs_handle_t handle) { (void)handle; return ESP_OK; }
esp_err_t nvs_flash_init_partition(const char *partition) { (void)partition; return ESP_OK; }
esp_err_t nvs_flash_deinit_partition(const char *partition) { (void)partition; return ESP_OK; }
esp_err_t nvs_flash_erase_partition(const char *partition)
{
    (void)partition;
    const char *keys[] = { "root", "hmac", "catalog" };
    for (unsigned i = 0; i < 3; ++i) nvs_erase_key(1, keys[i]);
    return ESP_OK;
}
struct sim_queue { SDL_mutex *mutex; SDL_cond *condition; uint8_t *bytes; size_t size; bool full; };
QueueHandle_t xQueueCreateStatic(unsigned count, size_t size, uint8_t *bytes, StaticQueue_t *storage)
{
    (void)count; (void)storage; QueueHandle_t queue = calloc(1, sizeof(*queue));
    queue->mutex = SDL_CreateMutex(); queue->condition = SDL_CreateCond(); queue->bytes = bytes; queue->size = size; return queue;
}
int xQueueReceive(QueueHandle_t queue, void *output, TickType_t wait)
{
    SDL_LockMutex(queue->mutex);
    while (!queue->full) {
        if (!wait) { SDL_UnlockMutex(queue->mutex); return 0; }
        if (wait == portMAX_DELAY) SDL_CondWait(queue->condition, queue->mutex);
        else if (SDL_CondWaitTimeout(queue->condition, queue->mutex, wait) == SDL_MUTEX_TIMEDOUT) { SDL_UnlockMutex(queue->mutex); return 0; }
    }
    memcpy(output, queue->bytes, queue->size); queue->full = false;
    SDL_CondBroadcast(queue->condition); SDL_UnlockMutex(queue->mutex); return pdTRUE;
}
int xQueueSend(QueueHandle_t queue, const void *input, TickType_t wait)
{
    SDL_LockMutex(queue->mutex);
    while (queue->full) {
        if (!wait) { SDL_UnlockMutex(queue->mutex); return 0; }
        SDL_CondWait(queue->condition, queue->mutex);
    }
    memcpy(queue->bytes, input, queue->size); queue->full = true;
    SDL_CondBroadcast(queue->condition); SDL_UnlockMutex(queue->mutex); return pdTRUE;
}
static int run_task(void *argument) { void (*function)(void *) = argument; function(NULL); return 0; }
SDL_Thread *xTaskCreateStatic(void (*function)(void *), const char *name, unsigned stack,
                            void *argument, unsigned priority, StackType_t *memory, StaticTask_t *storage)
{
    (void)stack; (void)argument; (void)priority; (void)memory; (void)storage;
    SDL_Thread *thread = SDL_CreateThread(run_task, name, (void *)function); SDL_DetachThread(thread); return thread;
}
esp_err_t store_init(void) { settings = cJSON_CreateObject(); return ESP_OK; }
esp_err_t store_save_settings(void) { return ESP_OK; }
esp_err_t store_write_json(const char *ns, const char *key, const cJSON *data)
{
    (void)ns; cJSON *copy = cJSON_Duplicate(data, true);
    if (cJSON_GetObjectItemCaseSensitive(settings, key)) cJSON_ReplaceItemInObjectCaseSensitive(settings, key, copy);
    else cJSON_AddItemToObject(settings, key, copy);
    return ESP_OK;
}
esp_err_t store_read_json(const char *ns, const char *key, cJSON **output)
{ (void)ns; const cJSON *value = cJSON_GetObjectItemCaseSensitive(settings, key); *output = cJSON_Duplicate(value, true); return value ? ESP_OK : ESP_ERR_NVS_NOT_FOUND; }
esp_err_t store_read_string(const char *ns, const char *key, char *output, size_t size)
{
    (void)ns;const cJSON *value=cJSON_GetObjectItemCaseSensitive(settings,key);
    if(!value)return ESP_ERR_NVS_NOT_FOUND;
    if(strlen(value->valuestring)>=size)return ESP_ERR_INVALID_SIZE;
    strcpy(output,value->valuestring);return ESP_OK;
}
esp_err_t esp_littlefs_info(const char *label,size_t *total,size_t *used)
{ (void)label;(void)total;(void)used;return ESP_ERR_NOT_SUPPORTED; }
esp_err_t store_factory_reset(bool erase_vault) { if (erase_vault) nvs_flash_erase_partition("vault"); return ESP_OK; }
void nuka_post(int32_t event, const void *data, size_t size)
{
    (void)data; (void)size; if(event==NUKA_PROVISIONING_READY)provisioning_ready=true;
    if(event>=NUKA_VOICE_PREPARE&&event<=NUKA_VOICE_STOP)printf("[sim] voice.%s at_ms=%u\n",(const char *[]){"prepare","start","end","cancel","stop"}[event-NUKA_VOICE_PREPARE],SDL_GetTicks());
}
void sim_dispatch_events(void)
{ if(provisioning_ready){provisioning_ready=false;overlay_provisioning();} }
void power_activity(void) { screen_on = true; }
void power_reload_settings(void) { }
void power_screen_off(void) { screen_on = false; vault_lock(); }
bool power_screen_on(void) { return screen_on; }
void power_shutdown(const char *reason) { (void)reason; sim_running = false; }
void power_set_permission_pending(bool pending) { (void)pending; }
bool nuka_time_stale(void) { return false; }
esp_err_t nuka_time_init(void) { return ESP_OK; }
esp_err_t nuka_time_update(time_t epoch, const char *timezone)
{ (void)epoch; (void)timezone; return ESP_OK; }
esp_err_t nuka_time_sntp(const char *const *servers, unsigned count)
{ (void)servers; (void)count; return ESP_OK; }
esp_err_t nuka_time_poll(void) { return ESP_OK; }
bool hub_connected(void) { return true; }
void hub_send(const char *type, cJSON *data, bool request)
{ (void)request; char *text = cJSON_PrintUnformatted(data); printf("[sim] %s %s\n", type, text); free(text); cJSON_Delete(data); }
void hub_subscribe_detail(const char *topic, const char *id)
{ if (topic) printf("[sim] detail %s %s\n", topic, id); else puts("[sim] detail subscription cleared"); }
void hub_set_feature(const char *topic, bool enabled) { (void)topic; (void)enabled; }
bool hub_feature_enabled(const char *topic) { (void)topic; return true; }
void hub_set_card(const char *card) { (void)card; }
bool net_connected(void) { return true; }
int net_rssi(void) { return -55; }
const char *net_pair_code(void) { return "000000"; }
void net_start_provisioning(void)
{ puts("[sim] provisioning requested; no network action");nuka_post(NUKA_PROVISIONING_READY,NULL,0); }
const char *net_provision_ssid(void) { return "Nuka-0000"; }
const char *net_provision_url(void) { return "http://192.0.2.1/"; }
axp_status_t battery_status(void) { return (axp_status_t){ .pct = sim_battery_pct, .vbat_mv = 3900, .batt_present = true }; }
void battery_estimates(float *screen_hours,float *standby_hours)
{ *screen_hours=-1;*standby_hours=-1; }
esp_err_t axp_set_aldo1(bool on) { (void)on; return ESP_OK; }
esp_err_t audio_tone(void) { return ESP_OK; }
esp_err_t macro_execute(unsigned slot) { printf("[sim] macro %u\n", slot); return ESP_OK; }
bool usb_attached(void) { return false; }
bool usb_admin_active(void) { return vault_management_active(); }
bool usb_enter_admin(void) { return vault_management_begin() == ESP_OK; }
void usb_request_provisioning(void) { net_start_provisioning(); }
void usb_restart_download(void) { puts("[sim] download requested; no hardware action"); }
void usb_pair_decide(bool accept) { printf("[sim] pair decision %d\n", accept); }
void usb_vault_decide(bool accept) { printf("[sim] Vault confirmation %d\n", accept); }
void usb_type_text(const char *text) { printf("[sim] HID text %zu bytes; no keystrokes sent\n", strlen(text)); }
void usb_media_key(uint16_t code) { printf("[sim] media key %u\n", code); }
const lv_image_dsc_t *media_cover(const char *path) { (void)path; return NULL; }
void perf_hud_show(bool show) { printf("[sim] device performance HUD %s; hardware counters unavailable\n", show ? "requested" : "hidden"); }
