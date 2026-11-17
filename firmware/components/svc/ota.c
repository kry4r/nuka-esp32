// SPDX-License-Identifier: AGPL-3.0-or-later
#include "ota.h"
#include "battery.h"
#include "hub_client.h"
#include "nuka_events.h"
#include "nukapack.h"
#include "store.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_system.h"
#include "esp_task_wdt.h"
#include "esp_timer.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "mbedtls/sha256.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

enum { ACCEPT = 1, HUB_READY = 2, OFFER = 4, REJECT = 8 };
static SemaphoreHandle_t mutex;
static TaskHandle_t task;
static uint32_t pending_commands;
static cJSON *offered;
static cJSON *offer_queue;
static cJSON *pending_result;
static bool downloading, verifying;
static unsigned progress;
static int64_t confirm_deadline;

static void ota_task(void *arg);
static void wake(uint32_t command)
{
    xSemaphoreTake(mutex, portMAX_DELAY);
    pending_commands |= command;
    if (task) xTaskNotifyGive(task);
    else ESP_ERROR_CHECK(xTaskCreatePinnedToCore(ota_task, "ota", 6144, NULL, 3, &task, 0) == pdPASS ? ESP_OK : ESP_ERR_NO_MEM);
    xSemaphoreGive(mutex);
}

static esp_err_t clear_pending(void)
{
    nvs_handle_t handle;
    esp_err_t error = nvs_open("sys", NVS_READWRITE, &handle);
    if (error != ESP_OK) return error;
    error = nvs_erase_key(handle, "ota_pending");
    if (error == ESP_OK) error = nvs_commit(handle);
    nvs_close(handle); return error;
}

static esp_err_t remember(const cJSON *offer, uint32_t target)
{
    cJSON *data = cJSON_CreateObject();
    cJSON_AddStringToObject(data, "ver", cJSON_GetObjectItemCaseSensitive(offer, "ver")->valuestring);
    cJSON_AddStringToObject(data, "kind", cJSON_GetObjectItemCaseSensitive(offer, "kind")->valuestring);
    cJSON_AddNumberToObject(data, "target", target);
    esp_err_t error = store_write_json("sys", "ota_pending", data);
    cJSON_Delete(data); return error;
}

static esp_err_t load_pending(void)
{
    esp_err_t error = store_read_json("sys", "ota_pending", &pending_result);
    if (error == ESP_ERR_NVS_NOT_FOUND) return ESP_OK;
    if (error != ESP_OK) return error;
    const cJSON *version = cJSON_GetObjectItemCaseSensitive(pending_result, "ver");
    const cJSON *kind = cJSON_GetObjectItemCaseSensitive(pending_result, "kind");
    const cJSON *target = cJSON_GetObjectItemCaseSensitive(pending_result, "target");
    if (!cJSON_IsObject(pending_result) || !cJSON_IsString(version) || !cJSON_IsString(kind) ||
        (strcmp(kind->valuestring, "app") && strcmp(kind->valuestring, "assets")) ||
        !cJSON_IsNumber(target) || !isfinite(target->valuedouble) ||
        target->valuedouble < 0 || target->valuedouble > UINT32_MAX ||
        floor(target->valuedouble) != target->valuedouble ||
        (!strcmp(kind->valuestring, "assets") && target->valuedouble > 1))
        return ESP_ERR_INVALID_ARG;
    return ESP_OK;
}

static void publish(const cJSON *offer, bool result, bool success, const char *reason)
{
    cJSON *data = cJSON_CreateObject();
    cJSON_AddStringToObject(data, "ver", cJSON_GetObjectItemCaseSensitive(offer, "ver")->valuestring);
    cJSON_AddStringToObject(data, "kind", cJSON_GetObjectItemCaseSensitive(offer, "kind")->valuestring);
    if (result) {
        cJSON_AddBoolToObject(data, "ok", success);
        if (!success) cJSON_AddStringToObject(data, "reason", reason);
    } else cJSON_AddNumberToObject(data, "pct", progress);
    hub_send(result ? "ota.result" : "ota.progress", data, false);
}

static esp_err_t download(const cJSON *offer, const char **reason)
{
    bool assets = !strcmp(cJSON_GetObjectItemCaseSensitive(offer, "kind")->valuestring, "assets");
    size_t size = cJSON_GetObjectItemCaseSensitive(offer, "size")->valuedouble;
    const esp_partition_t *partition = assets ?
        esp_partition_find_first(ESP_PARTITION_TYPE_DATA, 0x40, "assets") : esp_ota_get_next_update_partition(NULL);
    if (!partition || !size || size > (assets ? NUKAPACK_BANK_SIZE : partition->size)) {
        *reason = "invalid_size"; return ESP_ERR_INVALID_SIZE;
    }
    const char *url = cJSON_GetObjectItemCaseSensitive(offer, "url")->valuestring;
    esp_http_client_handle_t http = hub_http_client(url);
    if (!http) { *reason = "origin_rejected"; return ESP_ERR_INVALID_ARG; }
    esp_http_client_set_timeout_ms(http, 5000);
    esp_err_t error = esp_http_client_open(http, 0);
    esp_ota_handle_t app = 0;
    unsigned slot = 1u - nuka_settings.asset_slot;
    size_t offset = assets ? slot * NUKAPACK_BANK_SIZE : 0;
    mbedtls_sha256_context sha; mbedtls_sha256_init(&sha);
    uint8_t *buffer = NULL;
    bool remembered = false;
    *reason = "download_failed";
    if (error != ESP_OK) goto done;
    esp_task_wdt_reset();
    int64_t content_length = esp_http_client_fetch_headers(http);
    if (content_length < 0 || esp_http_client_get_status_code(http) != 200) {
        error = ESP_FAIL; goto done;
    }
    if (content_length && content_length != size) {
        *reason = "invalid_size"; error = ESP_ERR_INVALID_SIZE; goto done;
    }
    esp_task_wdt_reset();
    if (assets) {
        size_t erase_size = (size + 4095u) & ~4095u;
        error = esp_partition_erase_range(partition, offset, erase_size);
    } else error = esp_ota_begin(partition, size, &app);
    if (error != ESP_OK) goto done;
    buffer = heap_caps_malloc(4096, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!buffer) { error = ESP_ERR_NO_MEM; goto done; }
    error = mbedtls_sha256_starts(&sha, 0) == 0 ? ESP_OK : ESP_FAIL;
    if (error != ESP_OK) goto done;
    size_t received = 0;
    while (received < size) {
        esp_task_wdt_reset();
        size_t wanted = size - received; if (wanted > 4096) wanted = 4096;
        int count = esp_http_client_read(http, (char *)buffer, wanted);
        if (count <= 0) { error = ESP_FAIL; goto done; }
        error = assets ? esp_partition_write(partition, offset + received, buffer, count) :
            esp_ota_write(app, buffer, count);
        if (error != ESP_OK) goto done;
        if (mbedtls_sha256_update(&sha, buffer, count)) { error = ESP_FAIL; goto done; }
        received += count;
        unsigned pct = received * 100u / size;
        if (pct != progress) { progress = pct; publish(offer, false, false, NULL); }
        vTaskDelay(1);
    }
    // Content-Length may be absent for a chunked response; reject extra body bytes.
    if (esp_http_client_read(http, (char *)buffer, 1) != 0 || !esp_http_client_is_complete_data_received(http)) {
        *reason = "invalid_size"; error = ESP_ERR_INVALID_SIZE; goto done;
    }
    uint8_t digest[32]; char hex[65]; const char digits[] = "0123456789abcdef";
    if (mbedtls_sha256_finish(&sha, digest)) { error = ESP_FAIL; goto done; }
    for (unsigned i = 0; i < 32; ++i) { hex[2*i] = digits[digest[i] >> 4]; hex[2*i+1] = digits[digest[i] & 15]; }
    hex[64] = 0;
    if (strcmp(hex, cJSON_GetObjectItemCaseSensitive(offer, "sha256")->valuestring)) {
        *reason = "sha_mismatch"; error = ESP_ERR_INVALID_CRC; goto done;
    }
    if (assets) {
        *reason = "invalid_assets";
        error = nukapack_check_bank(slot, size, cJSON_GetObjectItemCaseSensitive(offer, "ver")->valuestring);
    } else {
        *reason = "invalid_image";
        error = esp_ota_end(app); app = 0;
    }
    if (error != ESP_OK) goto done;
    // Keep the requested version across both the candidate boot and a bootloader rollback.
    *reason = "commit_failed";
    error = remember(offer, assets ? slot : partition->address);
    if (error != ESP_OK) goto done;
    remembered = true;
    error = assets ? store_write_u32("sys", "asset_slot", slot) : esp_ota_set_boot_partition(partition);
done:
    if (error != ESP_OK && remembered) {
        esp_err_t cleared = clear_pending();
        if (cleared != ESP_OK) error = cleared;
    }
    if (app) esp_ota_abort(app);
    free(buffer); mbedtls_sha256_free(&sha);
    esp_http_client_close(http); esp_http_client_cleanup(http); return error;
}

static void ota_task(void *arg)
{
    ESP_ERROR_CHECK(esp_task_wdt_add(NULL));
    for (;;) {
        esp_task_wdt_reset();
        xSemaphoreTake(mutex, portMAX_DELAY);
        uint32_t commands = pending_commands; pending_commands = 0;
        xSemaphoreGive(mutex);
        esp_err_t error = ESP_OK;
        if (commands & HUB_READY) {
            if (verifying) {
                error = esp_ota_mark_app_valid_cancel_rollback();
                if (error == ESP_OK) verifying = false;
            }
            if (error == ESP_OK && pending_result) {
                bool assets = !strcmp(cJSON_GetObjectItemCaseSensitive(pending_result, "kind")->valuestring, "assets");
                uint32_t target = cJSON_GetObjectItemCaseSensitive(pending_result, "target")->valuedouble;
                bool success = assets ? target == nuka_settings.asset_slot &&
                    !strcmp(nuka_assets_version(), cJSON_GetObjectItemCaseSensitive(pending_result, "ver")->valuestring) :
                    target == esp_ota_get_running_partition()->address;
                publish(pending_result, true, success, "rollback");
                error = clear_pending();
                if (error == ESP_OK) { cJSON_Delete(pending_result); pending_result = NULL; }
            }
        }
        if (verifying && esp_timer_get_time() >= confirm_deadline) {
            ESP_LOGE("ota", "No Hub handshake within 60 seconds; rolling back");
            error = esp_ota_mark_app_invalid_rollback_and_reboot();
        }
        if (commands & REJECT) {
            xSemaphoreTake(mutex, portMAX_DELAY);
            cJSON_Delete(offered); offered = NULL;
            xSemaphoreGive(mutex);
        }
        if (commands & ACCEPT) {
            xSemaphoreTake(mutex, portMAX_DELAY);
            cJSON *offer = offered; offered = NULL; downloading = offer != NULL; progress = 0;
            xSemaphoreGive(mutex);
            if (offer) {
                axp_status_t battery = battery_status();
                const char *reason = "power_required";
                if (battery.vbus_present || (battery.batt_present && battery.pct > 50))
                    error = download(offer, &reason);
                else error = ESP_ERR_INVALID_STATE;
                if (error != ESP_OK) publish(offer, true, false, reason);
                cJSON_Delete(offer);
                if (error == ESP_OK) {
                    // Reboot also releases every LVGL reference to the old resource bank.
                    vTaskDelay(pdMS_TO_TICKS(500)); esp_restart();
                }
                xSemaphoreTake(mutex, portMAX_DELAY); downloading = false; xSemaphoreGive(mutex);
            }
        }
        xSemaphoreTake(mutex, portMAX_DELAY);
        char *announcement = NULL;
        if (!offered && !downloading && cJSON_GetArraySize(offer_queue)) {
            offered = cJSON_DetachItemFromArray(offer_queue, 0);
            announcement = cJSON_PrintUnformatted(offered);
        }
        xSemaphoreGive(mutex);
        if (announcement) {
            nuka_post(NUKA_OTA_OFFER, announcement, strlen(announcement) + 1);
            cJSON_free(announcement);
        }
        if (error != ESP_OK) ESP_LOGE("ota", "operation failed: %s", esp_err_to_name(error));
        xSemaphoreTake(mutex, portMAX_DELAY);
        if (!verifying && !pending_commands) {
            ESP_ERROR_CHECK(esp_task_wdt_delete(NULL)); task = NULL;
            xSemaphoreGive(mutex); vTaskDelete(NULL);
        }
        xSemaphoreGive(mutex);
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(200));
    }
}

void ota_start(void)
{
    mutex = xSemaphoreCreateMutex();
    offer_queue = cJSON_CreateArray();
    ESP_ERROR_CHECK(mutex && offer_queue ? ESP_OK : ESP_ERR_NO_MEM);
    esp_ota_img_states_t state;
    esp_err_t error = esp_ota_get_state_partition(esp_ota_get_running_partition(), &state);
    if (error != ESP_ERR_NOT_FOUND) ESP_ERROR_CHECK(error);
    verifying = error == ESP_OK && state == ESP_OTA_IMG_PENDING_VERIFY;
    ESP_ERROR_CHECK(load_pending());
    confirm_deadline = 60000000;
    if (verifying) wake(0);
}
void ota_offer(const cJSON *data)
{
    xSemaphoreTake(mutex, portMAX_DELAY);
    cJSON_AddItemToArray(offer_queue, cJSON_Duplicate(data, true));
    xSemaphoreGive(mutex);
    wake(OFFER);
}
void ota_accept(bool accept)
{
    wake(accept ? ACCEPT : REJECT);
}
void ota_hub_ready(void) { wake(HUB_READY); }
bool ota_pending(void)
{
    xSemaphoreTake(mutex, portMAX_DELAY); bool pending = offered != NULL || downloading || cJSON_GetArraySize(offer_queue);
    xSemaphoreGive(mutex); return pending;
}
unsigned ota_progress(void) { return progress; }
