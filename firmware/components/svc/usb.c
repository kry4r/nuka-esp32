// SPDX-License-Identifier: AGPL-3.0-or-later
#include "usb.h"
#include "usb_descriptors.h"
#include "store.h"
#include "net.h"
#include "macro.h"
#include "diag.h"
#include "proto.h"
#include "nukapack.h"
#include "nuka_events.h"
#include "vault.h"
#include "vault_json.h"
#include "tinyusb.h"
#include "tinyusb_default_config.h"
#include "tinyusb_cdc_acm.h"
#include "tusb.h"
#include "esp_app_desc.h"
#include "esp_heap_caps.h"
#include "esp_mac.h"
#include "esp_system.h"
#include "esp_task_wdt.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "soc/rtc_cntl_reg.h"
#include "soc/soc.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "mbedtls/base64.h"
#include "mbedtls/platform_util.h"
#include "nvs.h"
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef enum { TX, PAIR_DECISION, VAULT_DECISION, HID_TEXT, HID_KEYS, HID_MEDIA } command_kind_t;
typedef struct {
    command_kind_t kind;
    char *text;
    bool accept;
    uint8_t modifiers, codes[6];
    uint16_t media;
} command_t;
typedef enum { PENDING_NONE, PENDING_PAIR, PENDING_EXPORT, PENDING_IMPORT } pending_kind_t;
static QueueHandle_t commands;
static TaskHandle_t cdc_task;
static bool attached, detached;
static DRAM_ATTR struct {
    pending_kind_t kind;
    char *id;
    int64_t deadline;
    cJSON *provisioning;
    uint8_t key[32], salt[16];
    vault_import_preview_t preview;
} pending;
static bool importing;
static uint32_t expected_seq;
static const uint8_t ascii_keys[128][2] = { HID_ASCII_TO_KEYCODE };

static void clear_string(char *text)
{ if (text) vault_free_buffer((uint8_t *)text, strlen(text) + 1); }
static char *internal_copy(const char *text)
{
    size_t size = strlen(text) + 1;
    char *copy = heap_caps_malloc(size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    memcpy(copy, text, size); return copy;
}
static esp_err_t transmit(const char *text)
{
    size_t length = strlen(text), sent = 0;
    while (sent < length) {
        if (!tud_cdc_n_connected(0)) return ESP_ERR_INVALID_STATE;
        sent += tinyusb_cdcacm_write_queue(TINYUSB_CDC_ACM_0,
            (const uint8_t *)text + sent, length - sent);
        esp_err_t error = tinyusb_cdcacm_write_flush(TINYUSB_CDC_ACM_0, pdMS_TO_TICKS(1000));
        if (error != ESP_OK) return error;
        esp_task_wdt_reset();
    }
    return ESP_OK;
}
static esp_err_t reply(const char *type, const char *id, const cJSON *data)
{
    cJSON *frame = cJSON_CreateObject();
    cJSON_AddStringToObject(frame, "t", type);
    if (id) cJSON_AddRawToObject(frame, "id", id);
    cJSON_AddItemReferenceToObject(frame, "d", (cJSON *)data);
    char *text = cJSON_PrintUnformatted(frame);
    size_t length = strlen(text);
    char *line = realloc(text, length + 2); line[length] = '\n'; line[length + 1] = 0;
    esp_err_t error = transmit(line); free(line); cJSON_Delete(frame); return error;
}
static esp_err_t ok(const char *id)
{
    cJSON *data = cJSON_CreateObject(); esp_err_t error = reply("ok", id, data);
    cJSON_Delete(data); return error;
}
static esp_err_t reject(const char *id, const char *code, const char *message)
{
    cJSON *data = cJSON_CreateObject();
    cJSON_AddStringToObject(data, "code", code); cJSON_AddStringToObject(data, "msg", message);
    esp_err_t error = reply("err", id, data); cJSON_Delete(data); return error;
}
void usb_send(const char *type, int request_id, const cJSON *data)
{
    if (!attached) return;
    cJSON *frame = cJSON_CreateObject(); cJSON_AddStringToObject(frame, "t", type);
    if (request_id >= 0) cJSON_AddNumberToObject(frame, "id", request_id);
    cJSON_AddItemReferenceToObject(frame, "d", (cJSON *)data);
    char *text = cJSON_PrintUnformatted(frame); size_t length = strlen(text);
    text = realloc(text, length + 2); text[length] = '\n'; text[length + 1] = 0;
    command_t command = { .kind = TX, .text = text };
    // The persistent ring retains logs even when a slow CDC reader fills this queue.
    if (xQueueSend(commands, &command, 0) != pdTRUE) free(text);
    xTaskNotifyGive(cdc_task);
    cJSON_Delete(frame);
}
static void cancel_pending(void)
{
    if (pending.kind != PENDING_NONE) {
        const char closed[] = "{\"action\":\"closed\"}";
        nuka_post(pending.kind == PENDING_PAIR ? NUKA_USB_PAIR : NUKA_USB_VAULT,
            closed, sizeof(closed));
    }
    clear_string(pending.id); cJSON_Delete(pending.provisioning);
    mbedtls_platform_zeroize(&pending, sizeof(pending));
}
static void show_confirmation(const char *action)
{
    cJSON *data = cJSON_CreateObject(); cJSON_AddStringToObject(data, "action", action);
    vault_guard_enter();
    cJSON_AddNumberToObject(data, "count", vault_entry_count());
    vault_guard_leave();
    cJSON_AddNumberToObject(data, "added", pending.preview.added);
    cJSON_AddNumberToObject(data, "skipped", pending.preview.skipped);
    char *text = cJSON_PrintUnformatted(data);
    nuka_post(NUKA_USB_VAULT, text, strlen(text) + 1);
    free(text); cJSON_Delete(data);
}
static esp_err_t chunks(const char *type, const char *id, const uint8_t *bytes, size_t length)
{
    char *encoded = heap_caps_malloc(4097, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!encoded) return ESP_ERR_NO_MEM;
    uint32_t seq = 0; size_t offset = 0;
    do {
        size_t part = length - offset; if (part > 3072) part = 3072;
        size_t written;
        int result = mbedtls_base64_encode((uint8_t *)encoded, 4097, &written, bytes + offset, part);
        if (result != 0) { free(encoded); return ESP_FAIL; }
        encoded[written] = 0;
        cJSON *data = cJSON_CreateObject(); cJSON_AddNumberToObject(data, "seq", seq++);
        cJSON_AddStringToObject(data, "b64", encoded);
        cJSON_AddBoolToObject(data, "last", offset + part == length);
        esp_err_t error = reply(type, id, data); cJSON_Delete(data);
        if (error != ESP_OK) { free(encoded); return error; }
        offset += part;
    } while (offset < length);
    free(encoded); return ok(id);
}
static esp_err_t info(const char *id)
{
    char dev_id[33], hub_id[33]; uint8_t mac[6]; char mac_text[18];
    esp_err_t error = store_read_string("prov", "dev_id", dev_id, sizeof(dev_id));
    if (error != ESP_OK) return error;
    char *token = heap_caps_malloc(1024, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!token) return ESP_ERR_NO_MEM;
    esp_err_t paired = store_read_string("prov", "dev_tok", token, 1024);
    vault_free_buffer((uint8_t *)token, 1024);
    if (paired != ESP_OK && paired != ESP_ERR_NVS_NOT_FOUND) return paired;
    error = esp_read_mac(mac, ESP_MAC_WIFI_STA);
    if (error != ESP_OK) return error;
    snprintf(mac_text, sizeof(mac_text), "%02x:%02x:%02x:%02x:%02x:%02x",
        mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    cJSON *data = cJSON_CreateObject();
    cJSON_AddStringToObject(data, "dev_id", dev_id);
    cJSON_AddStringToObject(data, "fw", esp_app_get_description()->version);
    cJSON_AddStringToObject(data, "nukapack", nuka_assets_version()); cJSON_AddNumberToObject(data, "proto", 1);
    cJSON_AddBoolToObject(data, "paired", paired == ESP_OK);
    cJSON_AddStringToObject(data, "mac", mac_text);
    cJSON_AddBoolToObject(data, "unlocked", vault_unlocked());
    cJSON_AddBoolToObject(data, "usb_admin", vault_management_active());
    if (paired == ESP_OK) {
        error = store_read_string("prov", "hub_id", hub_id, sizeof(hub_id));
        if (error == ESP_OK) cJSON_AddStringToObject(data, "hub_id", hub_id);
    }
    if (error == ESP_OK) error = reply("ok", id, data);
    cJSON_Delete(data); return error;
}
static bool string_field(const cJSON *data, const char *key)
{ return cJSON_IsString(cJSON_GetObjectItemCaseSensitive(data, key)); }
static bool provisioning_valid(const cJSON *data)
{
    const cJSON *wifi = cJSON_GetObjectItemCaseSensitive(data, "wifi");
    const cJSON *hubs = cJSON_GetObjectItemCaseSensitive(data, "hubs");
    if (!cJSON_IsArray(wifi) || cJSON_GetArraySize(wifi) < 1 || cJSON_GetArraySize(wifi) > 3 ||
        !cJSON_IsArray(hubs) || cJSON_GetArraySize(hubs) < 1) return false;
    const cJSON *item;
    cJSON_ArrayForEach(item, wifi) {
        if (!string_field(item, "ssid") || !string_field(item, "pass")) return false;
        size_t ssid = strlen(cJSON_GetObjectItemCaseSensitive(item, "ssid")->valuestring);
        if (!ssid || ssid > 32 || strlen(cJSON_GetObjectItemCaseSensitive(item, "pass")->valuestring) > 63) return false;
    }
    cJSON_ArrayForEach(item, hubs) {
        if (!cJSON_IsString(item) ||
            strlen(item->valuestring) > 511 ||
            (strncmp(item->valuestring, "ws://", 5) && strncmp(item->valuestring, "wss://", 6))) return false;
        const char *host = strstr(item->valuestring, "://") + 3;
        if (!*host || *host == '/' || *host == '?' || *host == '#') return false;
        for (const unsigned char *at = (const unsigned char *)item->valuestring; *at; ++at)
            if (*at <= 32 || *at >= 127) return false;
    }
    const char *fields[] = {"hub_id", "hub_name", "dev_token", "code"};
    for (unsigned i = 0; i < 4; ++i) if (!string_field(data, fields[i])) return false;
    const char *id = cJSON_GetObjectItemCaseSensitive(data, "hub_id")->valuestring;
    if (!id[0] || strlen(id) > 32) return false;
    for (const char *p = id; *p; ++p) if (!(*p >= 'a' && *p <= 'z') && !isdigit((unsigned char)*p) && *p != '_' && *p != '-') return false;
    const char *code = cJSON_GetObjectItemCaseSensitive(data, "code")->valuestring;
    if (strlen(code) != 6) return false;
    for (const char *p = code; *p; ++p) if (!isdigit((unsigned char)*p)) return false;
    const cJSON *access = cJSON_GetObjectItemCaseSensitive(data, "cf_access");
    if (access && (!cJSON_IsObject(access) || !string_field(access, "origin") ||
        !string_field(access, "client_id") || !string_field(access, "client_secret") ||
        strncmp(cJSON_GetObjectItemCaseSensitive(access, "origin")->valuestring, "https://", 8))) return false;
    return true;
}
static esp_err_t save_provisioning(void)
{
    const cJSON *data = pending.provisioning, *wifi = cJSON_GetObjectItemCaseSensitive(data, "wifi");
    nvs_handle_t handle; esp_err_t error = nvs_open("prov", NVS_READWRITE, &handle);
    if (error != ESP_OK) return error;
    for (unsigned i = 0; i < 3 && error == ESP_OK; ++i) {
        char key[8]; snprintf(key, sizeof(key), "wifi%u", i);
        const cJSON *item = cJSON_GetArrayItem(wifi, i);
        if (item) {
            char *text = cJSON_PrintUnformatted(item); error = nvs_set_str(handle, key, text);
            mbedtls_platform_zeroize(text, strlen(text)); free(text);
        } else { error = nvs_erase_key(handle, key); if (error == ESP_ERR_NVS_NOT_FOUND) error = ESP_OK; }
    }
    const char *json_keys[] = {"hubs", "cf_access"};
    for (unsigned i = 0; i < 2 && error == ESP_OK; ++i) {
        const cJSON *item = cJSON_GetObjectItemCaseSensitive(data, json_keys[i]);
        if (item) {
            char *text = cJSON_PrintUnformatted(item); error = nvs_set_str(handle, json_keys[i], text);
            mbedtls_platform_zeroize(text, strlen(text)); free(text);
        } else { error = nvs_erase_key(handle, json_keys[i]); if (error == ESP_ERR_NVS_NOT_FOUND) error = ESP_OK; }
    }
    const char *input[] = {"hub_id", "hub_name", "dev_token"}, *output[] = {"hub_id", "hub_name", "dev_tok"};
    for (unsigned i = 0; i < 3 && error == ESP_OK; ++i)
        error = nvs_set_str(handle, output[i], cJSON_GetObjectItemCaseSensitive(data, input[i])->valuestring);
    if (error == ESP_OK) error = nvs_commit(handle);
    nvs_close(handle); return error;
}
static bool decode_base64(const char *text, uint8_t **bytes, size_t *length)
{
    size_t input = strlen(text), capacity = input / 4 * 3 + 1;
    uint8_t *decoded = heap_caps_malloc(capacity, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!decoded) return false;
    if (input % 4 || mbedtls_base64_decode(decoded, capacity, length, (const uint8_t *)text, input)) {
        vault_free_buffer(decoded, capacity); return false;
    }
    char *canonical = heap_caps_malloc(input + 1, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!canonical) { vault_free_buffer(decoded, capacity); return false; }
    size_t written;
    bool valid = mbedtls_base64_encode((uint8_t *)canonical, input + 1, &written, decoded, *length) == 0 &&
        written == input && memcmp(canonical, text, input) == 0;
    vault_free_buffer((uint8_t *)canonical, input + 1);
    if (!valid) { vault_free_buffer(decoded, capacity); return false; }
    *bytes = decoded; return true;
}
static bool key_field(const vault_json_document_t *document, int object, const char *name, uint8_t *out, size_t size)
{
    char *text = vault_json_string(document, vault_json_field(document, object, name));
    if (!text) return false;
    uint8_t *bytes = NULL; size_t length = 0;
    bool valid = decode_base64(text, &bytes, &length);
    if (valid) { valid = length == size; if (valid) memcpy(out, bytes, size); vault_free_buffer(bytes, length); }
    clear_string(text); return valid;
}
static esp_err_t handle_vault(const char *type, const char *id, const vault_json_document_t *doc, int data)
{
    if (!vault_management_active()) return reject(id, "forbidden", "Unlock Vault and enter USB management on the device");
    if (pending.kind != PENDING_NONE) return reject(id, "rate_limited", "A confirmation is pending");
    if (importing && !strncmp(type, "vault.export", 12)) return reject(id, "rate_limited", "A Vault import is in progress");
    if (!strcmp(type, "vault.export")) {
        uint8_t key[32], salt[16];
        bool valid = key_field(doc, data, "key_b64", key, sizeof(key)) && key_field(doc, data, "salt_b64", salt, sizeof(salt));
        if (valid) {
            memcpy(pending.key, key, sizeof(key)); memcpy(pending.salt, salt, sizeof(salt));
            pending.id = internal_copy(id); pending.kind = PENDING_EXPORT;
            pending.deadline = esp_timer_get_time() + 60000000; show_confirmation("export");
        }
        mbedtls_platform_zeroize(key, sizeof(key)); mbedtls_platform_zeroize(salt, sizeof(salt));
        return valid ? ESP_OK : reject(id, "bad_request", "Invalid backup key or salt");
    }
    esp_err_t error = ESP_ERR_INVALID_ARG;
    if (!strcmp(type, "vault.import.begin")) {
        uint8_t key[32]; uint32_t size;
        if (key_field(doc, data, "key_b64", key, sizeof(key)) &&
            vault_json_u32(doc, vault_json_field(doc, data, "size"), &size) && size >= 55)
            error = vault_import_begin(key, size);
        mbedtls_platform_zeroize(key, sizeof(key)); importing = error == ESP_OK; expected_seq = 0;
    } else if (!strcmp(type, "vault.import.chunk")) {
        uint32_t seq; char *text = vault_json_string(doc, vault_json_field(doc, data, "b64"));
        uint8_t *bytes = NULL; size_t length = 0;
        if (importing && text && vault_json_u32(doc, vault_json_field(doc, data, "seq"), &seq) &&
            seq == expected_seq && decode_base64(text, &bytes, &length)) {
            error = vault_import_chunk(bytes, length); ++expected_seq;
        }
        if (bytes) vault_free_buffer(bytes, length);
        clear_string(text);
    } else if (!strcmp(type, "vault.import.end")) {
        if (importing) error = vault_import_finish(&pending.preview);
    } else if (!strcmp(type, "vault.import.otpauth")) {
        int array = vault_json_field(doc, data, "uris"), count = vault_json_array_size(doc, array);
        if (count > 0) {
            char **uris = heap_caps_calloc(count, sizeof(char *), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
            if (!uris) return reject(id, "bad_request", "Vault input exceeds available internal RAM");
            bool valid = true;
            for (int i = 0; i < count; ++i) {
                uris[i] = vault_json_string(doc, vault_json_array_at(doc, array, i));
                if (!uris[i]) valid = false;
            }
            if (valid) error = vault_otpauth_preview((const char *const *)uris, count, &pending.preview);
            for (int i = 0; i < count; ++i) clear_string(uris[i]);
            vault_free_buffer((uint8_t *)uris, count * sizeof(char *));
        }
    }
    if (error != ESP_OK) {
        importing = false; vault_import_cancel();
        return reject(id, "bad_request", "Invalid Vault input");
    }
    if (!strcmp(type, "vault.import.end") || !strcmp(type, "vault.import.otpauth")) {
        importing = false; pending.kind = PENDING_IMPORT; pending.id = internal_copy(id);
        pending.deadline = esp_timer_get_time() + 60000000; show_confirmation("import");
        return ESP_OK;
    }
    return ok(id);
}
static bool copy_leaf(const vault_json_document_t *doc, int token, cJSON *parent, const char *name)
{
    if (token < 0) return true;
    char *raw = vault_json_raw(doc, token);
    if (!raw) return false;
    cJSON *value = raw[0] == '{' || raw[0] == '[' ? NULL : cJSON_Parse(raw);
    clear_string(raw);
    if (!value) return false;
    if (name) cJSON_AddItemToObject(parent, name, value);
    else cJSON_AddItemToArray(parent, value);
    return true;
}
static cJSON *copy_fields(const vault_json_document_t *doc, int token,
                          const char *const *names, size_t count)
{
    cJSON *object = cJSON_CreateObject();
    for (size_t i = 0; i < count; ++i) {
        if (!copy_leaf(doc, vault_json_field(doc, token, names[i]), object, names[i])) {
            cJSON_Delete(object); return NULL;
        }
    }
    return object;
}
static bool copy_array(const vault_json_document_t *doc, int token, cJSON *parent,
                       const char *name, const char *const *fields, size_t field_count)
{
    int array_token = vault_json_field(doc, token, name), count = vault_json_array_size(doc, array_token);
    if (count < 0) return false;
    cJSON *array = cJSON_AddArrayToObject(parent, name);
    for (int i = 0; i < count; ++i) {
        int item = vault_json_array_at(doc, array_token, i);
        if (fields) {
            if (!vault_json_object(doc, item)) return false;
            cJSON *object = copy_fields(doc, item, fields, field_count);
            if (!object) return false;
            cJSON_AddItemToArray(array, object);
        } else if (!copy_leaf(doc, item, array, NULL)) return false;
    }
    return true;
}
static bool request_data(const vault_json_document_t *doc, int token, const char *type, cJSON **out)
{
    *out = NULL;
    if (!strcmp(type, "diag.log")) {
        const char *const fields[] = {"on"};
        *out = copy_fields(doc, token, fields, 1);
        return *out != NULL;
    }
    if (!strcmp(type, "macro.set")) {
        const char *const fields[] = {"rev"}, *const item[] = {"slot", "label", "kind", "value"};
        *out = copy_fields(doc, token, fields, 1);
        return *out && copy_array(doc, token, *out, "macros", item, 4);
    }
    if (!strcmp(type, "prov.set")) {
        const char *const fields[] = {"hub_id", "hub_name", "dev_token", "code"};
        const char *const wifi[] = {"ssid", "pass"}, *const access[] = {"origin", "client_id", "client_secret"};
        *out = copy_fields(doc, token, fields, 4);
        if (!*out || !copy_array(doc, token, *out, "wifi", wifi, 2) ||
            !copy_array(doc, token, *out, "hubs", NULL, 0)) return false;
        int cf_access = vault_json_field(doc, token, "cf_access");
        if (cf_access >= 0) {
            if (!vault_json_object(doc, cf_access)) return false;
            cJSON *object = copy_fields(doc, cf_access, access, 3);
            if (!object) return false;
            cJSON_AddItemToObject(*out, "cf_access", object);
        }
    }
    return true;
}
static esp_err_t dispatch(const char *line, size_t length)
{
    vault_json_document_t *document;
    if (!proto_json_text_valid(line, length)) return reject("0", "bad_request", "JSON strings cannot contain NUL");
    if (vault_json_open(line, length, &document) != ESP_OK) return reject("0", "bad_request", "Invalid JSON frame");
    char *type = vault_json_string(document, vault_json_field(document, 0, "t"));
    char *id = vault_json_raw(document, vault_json_field(document, 0, "id"));
    int data_token = vault_json_field(document, 0, "d");
    char *end = NULL; double number = id ? strtod(id, &end) : NAN;
    esp_err_t error;
    if (!type || !id || !*id || *end || !isfinite(number) || floor(number) != number ||
        !vault_json_object(document, data_token) || vault_json_field(document, 0, "v") >= 0 ||
        vault_json_field(document, 0, "ts") >= 0) {
        error = reject("0", "bad_request", "Expected t, integer id and object d"); goto done;
    }
    if (!strncmp(type, "vault.", 6)) { error = handle_vault(type, id, document, data_token); goto done; }
    // Project known fields so unknown nested input never reaches cJSON's recursive parser.
    cJSON *data;
    if (!request_data(document, data_token, type, &data)) {
        cJSON_Delete(data); error = reject(id, "bad_request", "Invalid CDC input"); goto done;
    }
    error = ESP_OK;
    if (!strcmp(type, "info")) error = info(id);
    else if (!strcmp(type, "prov.set")) {
        if (!provisioning_valid(data)) error = reject(id, "bad_request", "Invalid provisioning input");
        else if (pending.kind != PENDING_NONE) error = reject(id, "rate_limited", "A confirmation is pending");
        else {
            pending.kind = PENDING_PAIR; pending.provisioning = data; data = NULL;
            pending.id = internal_copy(id); pending.deadline = esp_timer_get_time() + 60000000;
            cJSON *shown = cJSON_CreateObject();
            cJSON_AddStringToObject(shown, "hub_name", cJSON_GetObjectItemCaseSensitive(pending.provisioning, "hub_name")->valuestring);
            cJSON_AddStringToObject(shown, "code", cJSON_GetObjectItemCaseSensitive(pending.provisioning, "code")->valuestring);
            char *text = cJSON_PrintUnformatted(shown); nuka_post(NUKA_USB_PAIR, text, strlen(text) + 1);
            free(text); cJSON_Delete(shown);
        }
    } else if (!strcmp(type, "diag.info")) {
        cJSON *snapshot = diag_snapshot(); error = reply("ok", id, snapshot); cJSON_Delete(snapshot);
    } else if (!strcmp(type, "diag.log")) {
        cJSON *on = cJSON_GetObjectItemCaseSensitive(data, "on");
        if (!cJSON_IsBool(on)) error = reject(id, "bad_request", "Expected boolean on");
        else { diag_stream(cJSON_IsTrue(on)); error = ok(id); }
    } else if (!strcmp(type, "diag.dump_log")) {
        uint8_t *bytes; size_t size; error = diag_dump(&bytes, &size);
        if (error == ESP_OK) { error = chunks("log.chunk", id, bytes, size); free(bytes); }
    } else if (!strcmp(type, "sys.reboot") || !strcmp(type, "sys.download")) {
        error = ok(id);
        if (error == ESP_OK) { vTaskDelay(pdMS_TO_TICKS(100));
            if (!strcmp(type, "sys.download")) usb_restart_download(); else esp_restart(); }
    } else if (!strcmp(type, "macro.set")) {
        error = macro_validate(data) ? ESP_OK : ESP_ERR_INVALID_ARG;
        if (error == ESP_OK) { error = macro_apply(data); if (error == ESP_OK) error = ok(id); }
        else error = reject(id, "bad_request", "Invalid macros");
    } else error = reject(id, "not_found", "Unknown CDC command");
    cJSON_Delete(data);
done:
    if (error != ESP_OK && id && tud_cdc_n_connected(0))
        reject(id, "internal", "USB operation failed");
    clear_string(type); clear_string(id); vault_json_close(document); return error;
}
static esp_err_t decision(bool accept, bool pair)
{
    if ((pair && pending.kind != PENDING_PAIR) || (!pair && pending.kind != PENDING_EXPORT && pending.kind != PENDING_IMPORT)) return ESP_OK;
    esp_err_t error;
    if (!accept) { vault_import_cancel(); error = reject(pending.id, "rejected", "Cancelled on device"); }
    else if (pending.kind == PENDING_PAIR) {
        error = save_provisioning();
        if (error == ESP_OK) { error = ok(pending.id); net_reload(); }
    } else if (!vault_management_active()) error = reject(pending.id, "forbidden", "USB management expired");
    else if (pending.kind == PENDING_EXPORT) {
        uint8_t *encrypted; size_t size;
        error = vault_export(pending.key, pending.salt, &encrypted, &size);
        if (error == ESP_OK) { error = chunks("vault.chunk", pending.id, encrypted, size); vault_free_buffer(encrypted, size); }
    } else {
        error = vault_import_commit();
        if (error == ESP_OK) {
            cJSON *data = cJSON_CreateObject(); cJSON_AddNumberToObject(data, "added", pending.preview.added);
            cJSON_AddNumberToObject(data, "skipped", pending.preview.skipped);
            error = reply("ok", pending.id, data); cJSON_Delete(data);
        }
    }
    if (error != ESP_OK) {
        vault_import_cancel();
        reject(pending.id, "internal", "USB operation failed");
    }
    cancel_pending(); return error;
}
static bool hid_ready(unsigned instance)
{
    int64_t deadline = esp_timer_get_time() + 1000000;
    while (attached && !tud_hid_n_ready(instance) && esp_timer_get_time() < deadline) vTaskDelay(pdMS_TO_TICKS(5));
    return attached && tud_hid_n_ready(instance);
}
static esp_err_t keyboard(uint8_t modifiers, const uint8_t codes[6])
{
    if (!hid_ready(0)) return ESP_ERR_TIMEOUT;
    return tud_hid_n_keyboard_report(0, 0, modifiers, codes) ? ESP_OK : ESP_FAIL;
}
static esp_err_t type_text(const char *text)
{
    const uint8_t release[6] = {0};
    for (const unsigned char *p = (const unsigned char *)text; *p; ++p)
        if (*p >= 128 || !ascii_keys[*p][1]) return ESP_ERR_INVALID_ARG;
    for (const unsigned char *p = (const unsigned char *)text; *p; ++p) {
        uint8_t codes[6] = { ascii_keys[*p][1] };
        esp_err_t error = keyboard(ascii_keys[*p][0] ? KEYBOARD_MODIFIER_LEFTSHIFT : 0, codes);
        if (error == ESP_OK) error = keyboard(0, release);
        if (error != ESP_OK) return error;
        esp_task_wdt_reset();
    }
    return ESP_OK;
}
static esp_err_t run_command(const command_t *command)
{
    switch (command->kind) {
    case TX: return transmit(command->text);
    case PAIR_DECISION: return decision(command->accept, true);
    case VAULT_DECISION: return decision(command->accept, false);
    case HID_TEXT: return type_text(command->text);
    case HID_KEYS: return keyboard(command->modifiers, command->codes);
    case HID_MEDIA:
        if (!hid_ready(1)) return ESP_ERR_TIMEOUT;
        if (!tud_hid_n_report(1, 0, &command->media, sizeof(command->media))) return ESP_FAIL;
        if (!hid_ready(1)) return ESP_ERR_TIMEOUT;
        return tud_hid_n_report(1, 0, &(uint16_t){0}, sizeof(uint16_t)) ? ESP_OK : ESP_FAIL;
    }
    __builtin_unreachable();
}
static void runtime_error(esp_err_t error)
{
    if (error == ESP_OK) return;
    diag_stream(false); ESP_LOGW("usb", "operation failed: %s", esp_err_to_name(error));
}
static void usb_task(void *arg)
{
    ESP_ERROR_CHECK(esp_task_wdt_add(NULL));
    char *line = heap_caps_malloc(8192, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    ESP_ERROR_CHECK(line ? ESP_OK : ESP_ERR_NO_MEM);
    uint8_t *bytes = heap_caps_malloc(512, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    ESP_ERROR_CHECK(bytes ? ESP_OK : ESP_ERR_NO_MEM);
    size_t used = 0; bool discard = false;
    while (true) {
        esp_task_wdt_reset();
        esp_err_t error = ESP_OK; command_t command;
        if (detached) {
            detached = false; cancel_pending(); importing = false;
            vault_import_cancel(); diag_stream(false);
            mbedtls_platform_zeroize(line, used); used = 0; discard = false;
        }
        if (xQueueReceive(commands, &command, 0)) {
            error = run_command(&command);
            if (command.kind == HID_TEXT) clear_string(command.text); else free(command.text);
            runtime_error(error);
        }
        size_t count;
        tinyusb_cdcacm_read(TINYUSB_CDC_ACM_0, bytes, 512, &count);
        for (size_t i = 0; i < count; ++i) {
            if (bytes[i] == '\n') {
                if (discard) error = reject("0", "bad_request", "CDC line exceeds 8192 bytes or contains NUL");
                else { line[used] = 0; error = dispatch(line, used); }
                runtime_error(error);
                mbedtls_platform_zeroize(line, used); used = 0; discard = false;
            } else if (!discard) {
                if (used == 8191 || bytes[i] == 0) discard = true;
                else line[used++] = bytes[i];
            }
        }
        mbedtls_platform_zeroize(bytes, 512);
        if (pending.kind != PENDING_NONE && (esp_timer_get_time() >= pending.deadline ||
            (pending.kind != PENDING_PAIR && !vault_management_active()))) {
            error = reject(pending.id, "timeout", "Device confirmation timed out");
            vault_import_cancel(); cancel_pending();
            runtime_error(error);
        }
        if (importing && !vault_management_active()) { importing = false; vault_import_cancel(); }
        if (!count) ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(20));
        else vTaskDelay(1);
    }
}
static void rx_callback(int interface, cdcacm_event_t *event)
{
    // TinyUSB's RX FIFO applies USB backpressure until this task consumes it.
    xTaskNotifyGive(cdc_task);
}
static void usb_event(tinyusb_event_t *event, void *arg)
{
    if (event->id == TINYUSB_EVENT_ATTACHED) attached = true;
    if (event->id == TINYUSB_EVENT_DETACHED) {
        attached = false; detached = true;
    }
}
static void usb_shutdown(void)
{
    // ROM must see a detached PHY before taking over TinyUSB's controller.
    tud_disconnect();
    vTaskDelay(pdMS_TO_TICKS(100));
    ESP_ERROR_CHECK(tinyusb_driver_uninstall());
}
void usb_start(void)
{
    commands = xQueueCreate(20, sizeof(command_t));
    ESP_ERROR_CHECK(commands ? ESP_OK : ESP_ERR_NO_MEM);
    tinyusb_config_t config = TINYUSB_DEFAULT_CONFIG(usb_event);
    usb_descriptors(&config.descriptor); config.pm_lock_enable = true;
    ESP_ERROR_CHECK(tinyusb_driver_install(&config));
    const tinyusb_config_cdcacm_t cdc = {.cdc_port = TINYUSB_CDC_ACM_0};
    ESP_ERROR_CHECK(tinyusb_cdcacm_init(&cdc));
    ESP_ERROR_CHECK(xTaskCreatePinnedToCore(usb_task, "usb", 4096, NULL, 5, &cdc_task, 0) == pdPASS ? ESP_OK : ESP_ERR_NO_MEM);
    ESP_ERROR_CHECK(tinyusb_cdcacm_register_callback(TINYUSB_CDC_ACM_0, CDC_EVENT_RX, rx_callback));
    ESP_ERROR_CHECK(esp_register_shutdown_handler(usb_shutdown));
}
void usb_pair_decide(bool accept)
{ command_t command = {.kind = PAIR_DECISION, .accept = accept}; xQueueSend(commands, &command, portMAX_DELAY); }
void usb_vault_decide(bool accept)
{ command_t command = {.kind = VAULT_DECISION, .accept = accept}; xQueueSend(commands, &command, portMAX_DELAY); }
bool usb_enter_admin(void) { return vault_management_begin() == ESP_OK; }
void usb_request_provisioning(void) { net_start_provisioning(); }
void usb_restart_download(void)
{
    REG_SET_BIT(RTC_CNTL_OPTION1_REG, RTC_CNTL_FORCE_DOWNLOAD_BOOT); esp_restart();
}
bool usb_attached(void) { return attached; }
bool usb_admin_active(void) { return vault_management_active(); }
void usb_type_text(const char *text)
{ command_t command = {.kind = HID_TEXT, .text = internal_copy(text)}; xQueueSend(commands, &command, portMAX_DELAY); }
void usb_keyboard_report(uint8_t modifiers, const uint8_t codes[6])
{
    command_t command = {.kind = HID_KEYS, .modifiers = modifiers}; memcpy(command.codes, codes, 6);
    xQueueSend(commands, &command, portMAX_DELAY);
}
void usb_media_key(uint16_t code)
{ command_t command = {.kind = HID_MEDIA, .media = code}; xQueueSend(commands, &command, portMAX_DELAY); }
