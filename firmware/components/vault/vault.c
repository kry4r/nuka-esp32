// SPDX-License-Identifier: AGPL-3.0-or-later
#include "vault_private.h"
#include <ctype.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_efuse.h"
#include "esp_hmac.h"
#include "esp_partition.h"
#include "nvs.h"
#include "nvs_flash.h"

typedef struct {
    uint32_t magic, version, iterations, count, next_id, failures;
    uint16_t unlock_seconds;
    uint8_t wipe_after, cooldown;
    uint8_t salt[16], nonce[12], check[16], tag[16];
} catalog_t;
typedef struct { uint32_t id, version, length; uint8_t nonce[12], tag[16]; } record_t;
static VAULT_INTERNAL struct {
    nvs_handle_t nvs;
    catalog_t header;
    uint8_t *catalog;
    size_t catalog_size;
    uint8_t key[32], import_key[32];
    uint8_t hmac_slot;
    uint32_t benchmark;
    bool configured, unlocked, importing, preview;
    int64_t unlock_until, admin_until, cooldown_until;
    vault_entry_t *entries, *pending;
    size_t count, pending_count, skipped;
    uint8_t *incoming;
    size_t incoming_size, incoming_used;
    size_t capacity;
} state;
static VAULT_INTERNAL StaticSemaphore_t mutex_storage;
static SemaphoreHandle_t mutex;
static const uint8_t check_value[16] = "nukavault-pin-v1";
#define CATALOG_MAGIC UINT32_C(0x314C564E)
#define RETURN(value) do { esp_err_t returned = (value); vault_guard_leave(); return returned; } while (0)

void vault_guard_enter(void) { xSemaphoreTakeRecursive(mutex, portMAX_DELAY); }
void vault_guard_leave(void) { xSemaphoreGiveRecursive(mutex); }
static int64_t now_us(void) { return esp_timer_get_time(); }
static void clear_entries(vault_entry_t *entries, size_t count)
{
    for (size_t i = 0; i < count; ++i) vault_entry_clear(entries + i);
    vault_clear_free(entries, count * sizeof(*entries));
}
void vault_free_buffer(uint8_t *buffer, size_t length) { vault_clear_free(buffer, length); }

static esp_err_t save_blob(uint8_t *blob, size_t length)
{
    esp_err_t error = nvs_set_blob(state.nvs, "catalog", blob, length);
    if (error == ESP_OK) error = nvs_commit(state.nvs);
    if (error == ESP_OK) {
        free(state.catalog); state.catalog = blob; state.catalog_size = length;
        memcpy(&state.header, blob, sizeof(state.header)); state.configured = true;
    } else free(blob);
    return error;
}
static esp_err_t save_header(const catalog_t *header)
{
    uint8_t *blob = malloc(state.catalog_size);
    if (!blob) return ESP_ERR_NO_MEM;
    memcpy(blob, state.catalog, state.catalog_size); memcpy(blob, header, sizeof(*header));
    return save_blob(blob, state.catalog_size);
}
static void aad_for(uint32_t id, uint32_t version, uint8_t aad[8])
{
    for (unsigned i = 0; i < 4; ++i) { aad[i] = id >> (i * 8); aad[i + 4] = version >> (i * 8); }
}
static esp_err_t root_key(uint8_t root[32])
{
    if (state.hmac_slot != UINT8_MAX)
        return esp_hmac_calculate((hmac_key_id_t)state.hmac_slot, "nukavault-v1", 12, root);
    size_t size = 32;
    esp_err_t error = nvs_get_blob(state.nvs, "root", root, &size);
    return error == ESP_OK && size != 32 ? ESP_ERR_INVALID_SIZE : error;
}
static esp_err_t seal_catalog(const vault_entry_t *entries, size_t count,
                               const uint8_t key[32], const catalog_t *header)
{
    size_t length = sizeof(*header);
    uint8_t *blob = malloc(length);
    if (!blob) return ESP_ERR_NO_MEM;
    memcpy(blob, header, length);
    ((catalog_t *)blob)->count = count;
    for (size_t i = 0; i < count; ++i) {
        size_t plain_size; char *plain = vault_json_write(entries + i, 1, &plain_size);
        record_t record = { .id = entries[i].id, .version = 1, .length = plain_size };
        esp_fill_random(record.nonce, sizeof(record.nonce));
        size_t next_size = length + sizeof(record) + plain_size;
        uint8_t *next = realloc(blob, next_size);
        if (!next) { free(blob); vault_clear_free(plain, plain_size + 1); return ESP_ERR_NO_MEM; }
        blob = next;
        uint8_t aad[8]; aad_for(record.id, record.version, aad);
        esp_err_t error = vault_crypto_seal(key, record.nonce, aad, sizeof(aad),
                                            (uint8_t *)plain, plain_size,
                                            blob + length + sizeof(record), record.tag);
        vault_clear_free(plain, plain_size + 1);
        if (error != ESP_OK) { free(blob); return error; }
        memcpy(blob + length, &record, sizeof(record)); length = next_size;
    }
    return save_blob(blob, length);
}
static esp_err_t read_catalog(const uint8_t key[32], vault_entry_t **entries)
{
    size_t count = state.header.count;
    vault_entry_t *result = count ? vault_alloc(count * sizeof(*result)) : NULL;
    size_t cursor = sizeof(catalog_t), initialized = 0;
    esp_err_t error = ESP_OK;
    while (initialized < count) {
        record_t record; memcpy(&record, state.catalog + cursor, sizeof(record)); cursor += sizeof(record);
        uint8_t *plain = vault_alloc(record.length + 1), aad[8];
        aad_for(record.id, record.version, aad);
        error = vault_crypto_open(key, record.nonce, aad, sizeof(aad), state.catalog + cursor,
                                   record.length, record.tag, plain);
        vault_entry_t *one = NULL; size_t one_count = 0;
        if (error == ESP_OK) error = vault_json_read((char *)plain, record.length, &one, &one_count);
        vault_clear_free(plain, record.length + 1);
        if (error == ESP_OK && one_count != 1) error = ESP_ERR_INVALID_RESPONSE;
        if (error != ESP_OK) { clear_entries(one, one_count); break; }
        result[initialized] = one[0]; result[initialized++].id = record.id;
        vault_clear_free(one, sizeof(*one)); cursor += record.length;
    }
    if (error != ESP_OK) { clear_entries(result, initialized); return error; }
    *entries = result; return ESP_OK;
}
static bool valid_catalog(void)
{
    const catalog_t *h = &state.header;
    if (state.catalog_size < sizeof(*h) || h->magic != CATALOG_MAGIC || h->version != 1 ||
        !h->iterations || !h->next_id || (h->wipe_after != 0 && h->wipe_after != 10 && h->wipe_after != 20) ||
        h->unlock_seconds < 60 || h->unlock_seconds > 600 || h->cooldown > 1) return false;
    size_t at = sizeof(*h);
    for (uint32_t i = 0; i < h->count; ++i) {
        if (state.catalog_size - at < sizeof(record_t)) return false;
        record_t record; memcpy(&record, state.catalog + at, sizeof(record)); at += sizeof(record);
        if (!record.id || record.id >= h->next_id || record.version != 1 ||
            !record.length || record.length > state.catalog_size - at) return false;
        at += record.length;
    }
    return at == state.catalog_size;
}
void vault_import_cancel(void)
{
    vault_guard_enter();
    clear_entries(state.pending, state.pending_count); state.pending = NULL; state.pending_count = 0;
    free(state.incoming); state.incoming = NULL;
    state.incoming_size = state.incoming_used = state.skipped = 0;
    state.importing = state.preview = false;
    mbedtls_platform_zeroize(state.import_key, sizeof(state.import_key));
    vault_guard_leave();
}
void vault_lock(void)
{
    vault_guard_enter();
    vault_import_cancel(); clear_entries(state.entries, state.count);
    state.entries = NULL; state.count = 0; state.unlocked = false;
    state.admin_until = state.unlock_until = 0;
    mbedtls_platform_zeroize(state.key, sizeof(state.key));
    vault_guard_leave();
}
static esp_err_t wipe(void)
{
    vault_lock();
    nvs_close(state.nvs);
    esp_err_t error = nvs_flash_deinit_partition("vault");
    if (error == ESP_OK) error = nvs_flash_erase_partition("vault");
    if (error == ESP_OK) error = nvs_flash_init_partition("vault");
    if (error == ESP_OK) error = nvs_open_from_partition("vault", "nukavault", NVS_READWRITE, &state.nvs);
    if (error == ESP_OK) {
        if (state.hmac_slot == UINT8_MAX) {
            uint8_t root[32]; esp_fill_random(root, sizeof(root));
            error = nvs_set_blob(state.nvs, "root", root, sizeof(root));
            mbedtls_platform_zeroize(root, sizeof(root));
        } else error = nvs_set_u8(state.nvs, "hmac", state.hmac_slot);
        if (error == ESP_OK) error = nvs_commit(state.nvs);
    }
    if (error == ESP_OK) {
        free(state.catalog); state.catalog = NULL; state.catalog_size = 0; state.configured = false;
        state.cooldown_until = 0;
        state.header = (catalog_t){ .magic = CATALOG_MAGIC, .version = 1,
            .iterations = state.benchmark, .next_id = 1, .wipe_after = 10, .unlock_seconds = 120 };
    }
    return error;
}
esp_err_t vault_init(void)
{
    mutex = xSemaphoreCreateRecursiveMutexStatic(&mutex_storage);
    ESP_ERROR_CHECK(mutex ? ESP_OK : ESP_ERR_NO_MEM);
    ESP_ERROR_CHECK(nvs_flash_init_partition("vault"));
    ESP_ERROR_CHECK(nvs_open_from_partition("vault", "nukavault", NVS_READWRITE, &state.nvs));
    state.capacity = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_NVS, "vault")->size;
    uint8_t hardware[32] = {0}, salt[16] = {0}, key[32];
    int64_t start = now_us();
    ESP_ERROR_CHECK(vault_crypto_key(hardware, "000000", 6, salt, 1000, key));
    int64_t elapsed = now_us() - start;
    state.benchmark = (uint32_t)(UINT64_C(1000) * 300000 / elapsed);
    mbedtls_platform_zeroize(key, sizeof(key));
    esp_err_t catalog_result = nvs_get_blob(state.nvs, "catalog", NULL, &state.catalog_size);
    if (catalog_result != ESP_OK && catalog_result != ESP_ERR_NVS_NOT_FOUND) return catalog_result;
    state.hmac_slot = UINT8_MAX;
    esp_err_t error = nvs_get_u8(state.nvs, "hmac", &state.hmac_slot);
    if (error != ESP_OK && error != ESP_ERR_NVS_NOT_FOUND) return error;
    if (state.hmac_slot != UINT8_MAX) {
        if (state.hmac_slot > 5 || esp_efuse_get_key_purpose((esp_efuse_block_t)(EFUSE_BLK_KEY0 + state.hmac_slot)) != ESP_EFUSE_KEY_PURPOSE_HMAC_UP)
            return ESP_ERR_INVALID_STATE;
    } else {
        size_t root_size = sizeof(hardware);
        error = nvs_get_blob(state.nvs, "root", hardware, &root_size);
        if (error == ESP_ERR_NVS_NOT_FOUND) {
            if (catalog_result == ESP_OK) return ESP_ERR_INVALID_STATE;
            esp_fill_random(hardware, sizeof(hardware));
            error = nvs_set_blob(state.nvs, "root", hardware, sizeof(hardware));
            if (error == ESP_OK) error = nvs_commit(state.nvs);
        } else if (error == ESP_OK && root_size != 32) error = ESP_ERR_INVALID_SIZE;
        mbedtls_platform_zeroize(hardware, sizeof(hardware));
        if (error != ESP_OK) return error;
    }
    if (catalog_result == ESP_ERR_NVS_NOT_FOUND) {
        state.header = (catalog_t){ .magic = CATALOG_MAGIC, .version = 1,
            .iterations = state.benchmark, .next_id = 1, .wipe_after = 10, .unlock_seconds = 120 };
        return ESP_OK;
    }
    if (state.catalog_size < sizeof(catalog_t)) return ESP_ERR_INVALID_SIZE;
    state.catalog = malloc(state.catalog_size);
    ESP_ERROR_CHECK(state.catalog ? ESP_OK : ESP_ERR_NO_MEM);
    ESP_ERROR_CHECK(nvs_get_blob(state.nvs, "catalog", state.catalog, &state.catalog_size));
    memcpy(&state.header, state.catalog, sizeof(state.header));
    if (!valid_catalog()) return ESP_ERR_INVALID_RESPONSE;
    state.configured = true;
    if (state.header.wipe_after && state.header.failures >= state.header.wipe_after) return wipe();
    if (state.header.cooldown) state.cooldown_until = now_us() + INT64_C(300000000);
    return ESP_OK;
}
esp_err_t vault_use_hardware_key(uint8_t slot)
{
    vault_guard_enter();
    if (state.configured) RETURN(ESP_ERR_INVALID_STATE);
    if (slot > 5 || esp_efuse_get_key_purpose((esp_efuse_block_t)(EFUSE_BLK_KEY0 + slot)) != ESP_EFUSE_KEY_PURPOSE_HMAC_UP)
        RETURN(ESP_ERR_INVALID_ARG);
    uint8_t key[32]; esp_err_t error = esp_hmac_calculate((hmac_key_id_t)slot, "nukavault-v1", 12, key);
    mbedtls_platform_zeroize(key, sizeof(key));
    if (error == ESP_OK) error = nvs_set_u8(state.nvs, "hmac", slot);
    if (error == ESP_OK && state.hmac_slot == UINT8_MAX) error = nvs_erase_key(state.nvs, "root");
    if (error == ESP_OK) error = nvs_commit(state.nvs);
    if (error == ESP_OK) state.hmac_slot = slot;
    RETURN(error);
}
vault_status_t vault_status(void)
{
    vault_guard_enter();
    int64_t remaining = state.cooldown_until - now_us();
    vault_status_t result = { .configured = state.configured, .hardware_key = state.hmac_slot != UINT8_MAX,
        .unlocked = state.unlocked, .usb_admin = state.unlocked && now_us() < state.admin_until,
        .failures = state.header.failures, .cooldown_s = remaining > 0 ? (remaining + 999999) / 1000000 : 0,
        .iterations = state.header.iterations, .wipe_after = state.header.wipe_after };
    result.unlock_seconds = state.header.unlock_seconds;
    vault_guard_leave(); return result;
}
static bool valid_pin(const char *pin, size_t length)
{
    if (length < 6 || length > 8) return false;
    for (size_t i = 0; i < length; ++i) if (pin[i] < '0' || pin[i] > '9') return false;
    return true;
}
esp_err_t vault_set_pin(const char *pin, size_t length)
{
    vault_guard_enter();
    if (!valid_pin(pin, length)) RETURN(ESP_ERR_INVALID_ARG);
    if (state.configured && !state.unlocked) RETURN(ESP_ERR_INVALID_STATE);
    catalog_t header = state.header;
    header.iterations = state.benchmark; header.failures = header.cooldown = 0;
    esp_fill_random(header.salt, sizeof(header.salt)); esp_fill_random(header.nonce, sizeof(header.nonce));
    uint8_t root[32], key[32];
    esp_err_t error = root_key(root);
    if (error == ESP_OK) error = vault_crypto_key(root, pin, length, header.salt, header.iterations, key);
    mbedtls_platform_zeroize(root, sizeof(root));
    if (error == ESP_OK) error = vault_crypto_seal(key, header.nonce, NULL, 0, check_value, 16, header.check, header.tag);
    if (error == ESP_OK) error = seal_catalog(state.entries, state.count, key, &header);
    if (error == ESP_OK) {
        memcpy(state.key, key, 32); state.unlocked = true;
        state.unlock_until = now_us() + (int64_t)header.unlock_seconds * 1000000;
        state.cooldown_until = 0;
    }
    mbedtls_platform_zeroize(key, sizeof(key)); RETURN(error);
}
esp_err_t vault_unlock(const char *pin, size_t length)
{
    vault_guard_enter();
    if (!valid_pin(pin, length)) RETURN(ESP_ERR_INVALID_ARG);
    if (!state.configured || state.unlocked) RETURN(ESP_ERR_INVALID_STATE);
    if (state.header.cooldown) RETURN(ESP_ERR_TIMEOUT);
    catalog_t header = state.header; ++header.failures;
    if (header.failures % 5 == 0) header.cooldown = 1;
    esp_err_t error = save_header(&header);
    if (error != ESP_OK) RETURN(error);
    if (header.cooldown) state.cooldown_until = now_us() + INT64_C(300000000);
    uint8_t root[32], key[32], check[16];
    error = root_key(root);
    if (error == ESP_OK) error = vault_crypto_key(root, pin, length, header.salt, header.iterations, key);
    mbedtls_platform_zeroize(root, sizeof(root));
    if (error == ESP_OK) error = vault_crypto_open(key, header.nonce, NULL, 0, header.check, 16, header.tag, check);
    if (error == ESP_OK && memcmp(check, check_value, 16)) error = ESP_ERR_INVALID_CRC;
    mbedtls_platform_zeroize(check, sizeof(check));
    vault_entry_t *entries = NULL;
    if (error == ESP_OK) error = read_catalog(key, &entries);
    if (error == ESP_OK) {
        header.failures = header.cooldown = 0;
        error = save_header(&header);
        if (error == ESP_OK) {
            state.entries = entries; state.count = header.count; state.unlocked = true;
            memcpy(state.key, key, 32); state.cooldown_until = 0;
            state.unlock_until = now_us() + (int64_t)header.unlock_seconds * 1000000;
        } else clear_entries(entries, header.count);
    } else if (header.wipe_after && header.failures >= header.wipe_after) {
        esp_err_t erased = wipe(); if (erased != ESP_OK) error = erased;
    }
    mbedtls_platform_zeroize(key, sizeof(key)); RETURN(error);
}
bool vault_unlocked(void)
{ vault_guard_enter(); bool unlocked = state.unlocked; vault_guard_leave(); return unlocked; }
void vault_activity(void)
{
    vault_guard_enter();
    if (state.unlocked) state.unlock_until = now_us() + (int64_t)state.header.unlock_seconds * 1000000;
    vault_guard_leave();
}
esp_err_t vault_tick(void)
{
    vault_guard_enter();
    int64_t now = now_us();
    if (state.unlocked && now >= state.unlock_until) vault_lock();
    if (state.admin_until && now >= state.admin_until) { state.admin_until = 0; vault_import_cancel(); }
    if (state.header.cooldown && now >= state.cooldown_until) {
        catalog_t header = state.header; header.cooldown = 0;
        esp_err_t error = save_header(&header);
        if (error != ESP_OK) RETURN(error);
        state.cooldown_until = 0;
    }
    RETURN(ESP_OK);
}
esp_err_t vault_set_policy(uint8_t wipe_after, uint16_t unlock_seconds)
{
    vault_guard_enter();
    if (!state.unlocked) RETURN(ESP_ERR_INVALID_STATE);
    if ((wipe_after != 0 && wipe_after != 10 && wipe_after != 20) || unlock_seconds < 60 || unlock_seconds > 600)
        RETURN(ESP_ERR_INVALID_ARG);
    catalog_t header = state.header; header.wipe_after = wipe_after; header.unlock_seconds = unlock_seconds;
    esp_err_t error = save_header(&header);
    if (error == ESP_OK) vault_activity();
    RETURN(error);
}
esp_err_t vault_management_begin(void)
{
    vault_guard_enter();
    if (!state.unlocked) RETURN(ESP_ERR_INVALID_STATE);
    state.admin_until = now_us() + INT64_C(120000000); vault_activity(); RETURN(ESP_OK);
}
bool vault_management_active(void)
{
    vault_guard_enter(); bool active = state.unlocked && now_us() < state.admin_until;
    vault_guard_leave(); return active;
}
size_t vault_entry_count(void) { return state.count; }
const vault_entry_t *vault_entry_at(size_t index) { return state.entries + index; }
static bool duplicate(const vault_entry_t *entry, const vault_entry_t *entries, size_t count)
{
    for (size_t i = 0; i < count; ++i)
        if (entry->kind == entries[i].kind && !strcmp(entry->name, entries[i].name)) return true;
    return false;
}
esp_err_t vault_add(const vault_entry_t *entry)
{
    vault_guard_enter();
    if (!state.unlocked) RETURN(ESP_ERR_INVALID_STATE);
    if (duplicate(entry, state.entries, state.count)) RETURN(ESP_ERR_INVALID_ARG);
    vault_entry_t *next = vault_alloc((state.count + 1) * sizeof(*next));
    if (state.count) memcpy(next, state.entries, state.count * sizeof(*next));
    next[state.count] = vault_entry_copy(entry); next[state.count].id = state.header.next_id;
    catalog_t header = state.header; ++header.next_id;
    esp_err_t error = seal_catalog(next, state.count + 1, state.key, &header);
    if (error == ESP_OK) {
        vault_clear_free(state.entries, state.count * sizeof(*next));
        state.entries = next; ++state.count; vault_activity();
    } else { vault_entry_clear(next + state.count); vault_clear_free(next, (state.count + 1) * sizeof(*next)); }
    RETURN(error);
}
esp_err_t vault_delete(uint32_t id)
{
    vault_guard_enter();
    if (!state.unlocked) RETURN(ESP_ERR_INVALID_STATE);
    size_t index = 0; while (index < state.count && state.entries[index].id != id) ++index;
    if (index == state.count) RETURN(ESP_ERR_NOT_FOUND);
    vault_entry_t *next = state.count > 1 ? vault_alloc((state.count - 1) * sizeof(*next)) : NULL;
    if (index) memcpy(next, state.entries, index * sizeof(*next));
    if (index + 1 < state.count) memcpy(next + index, state.entries + index + 1, (state.count - index - 1) * sizeof(*next));
    esp_err_t error = seal_catalog(next, state.count - 1, state.key, &state.header);
    if (error == ESP_OK) {
        vault_entry_clear(state.entries + index); vault_clear_free(state.entries, state.count * sizeof(*next));
        state.entries = next; --state.count; vault_activity();
    } else vault_clear_free(next, (state.count - 1) * sizeof(*next));
    RETURN(error);
}
esp_err_t vault_generate(const vault_generator_t *options, char password[65])
{
    if (options->length < 12 || options->length > 64 || !(options->upper || options->lower || options->digits || options->symbols))
        return ESP_ERR_INVALID_ARG;
    const char *sets[] = { "ABCDEFGHIJKLMNOPQRSTUVWXYZ", "abcdefghijklmnopqrstuvwxyz", "0123456789", "!\"#$%&'()*+,-./:;<=>?@[\\]^_`{|}~" };
    bool enabled[] = { options->upper, options->lower, options->digits, options->symbols };
    char alphabet[95]; size_t count = 0;
    for (unsigned set = 0; set < 4; ++set) if (enabled[set])
        for (const char *at = sets[set]; *at; ++at)
            if (!options->exclude_ambiguous || !strchr("Il1O0o|", *at)) alphabet[count++] = *at;
    for (size_t i = 0; i < options->length;) {
        uint8_t random; esp_fill_random(&random, 1);
        if (random < 256 - 256 % count) password[i++] = alphabet[random % count];
        mbedtls_platform_zeroize(&random, sizeof(random));
    }
    password[options->length] = 0; mbedtls_platform_zeroize(alphabet, sizeof(alphabet)); return ESP_OK;
}
esp_err_t vault_export(const uint8_t key[32], const uint8_t salt[16], uint8_t **encrypted, size_t *length)
{
    vault_guard_enter();
    if (!vault_management_active()) RETURN(ESP_ERR_INVALID_STATE);
    size_t plain_size; char *plain = vault_json_write(state.entries, state.count, &plain_size);
    uint8_t nonce[12]; esp_fill_random(nonce, sizeof(nonce));
    esp_err_t error = vault_backup_pack(key, salt, nonce, (uint8_t *)plain, plain_size, encrypted, length);
    vault_clear_free(plain, plain_size + 1); RETURN(error);
}
esp_err_t vault_import_begin(const uint8_t key[32], size_t size)
{
    vault_guard_enter();
    if (!vault_management_active()) RETURN(ESP_ERR_INVALID_STATE);
    if (size < 55 || size > state.capacity) RETURN(ESP_ERR_INVALID_SIZE);
    vault_import_cancel();
    state.incoming = malloc(size);
    if (!state.incoming) RETURN(ESP_ERR_NO_MEM);
    memcpy(state.import_key, key, 32); state.incoming_size = size; state.importing = true; RETURN(ESP_OK);
}
esp_err_t vault_import_chunk(const uint8_t *data, size_t length)
{
    vault_guard_enter();
    if (!vault_management_active() || !state.importing) RETURN(ESP_ERR_INVALID_STATE);
    if (length > state.incoming_size - state.incoming_used) { vault_import_cancel(); RETURN(ESP_ERR_INVALID_SIZE); }
    memcpy(state.incoming + state.incoming_used, data, length); state.incoming_used += length; RETURN(ESP_OK);
}
static void prepare_preview(vault_entry_t *entries, size_t count, vault_import_preview_t *preview)
{
    state.pending = entries; state.pending_count = state.skipped = 0;
    for (size_t i = 0; i < count; ++i) {
        if (duplicate(entries + i, state.entries, state.count) || duplicate(entries + i, entries, state.pending_count)) {
            vault_entry_clear(entries + i); ++state.skipped;
        } else {
            if (state.pending_count != i) { entries[state.pending_count] = entries[i]; memset(entries + i, 0, sizeof(*entries)); }
            ++state.pending_count;
        }
    }
    *preview = (vault_import_preview_t){ state.pending_count, state.skipped }; state.preview = true;
}
esp_err_t vault_import_finish(vault_import_preview_t *preview)
{
    vault_guard_enter();
    if (!vault_management_active() || !state.importing) RETURN(ESP_ERR_INVALID_STATE);
    if (state.incoming_used != state.incoming_size) { vault_import_cancel(); RETURN(ESP_ERR_INVALID_SIZE); }
    uint8_t *plain = NULL; size_t plain_size = 0;
    esp_err_t error = vault_backup_open(state.import_key, state.incoming, state.incoming_size, &plain, &plain_size);
    vault_entry_t *entries = NULL; size_t count = 0;
    if (error == ESP_OK) error = vault_json_read((char *)plain, plain_size, &entries, &count);
    vault_clear_free(plain, plain ? plain_size + 1 : 0);
    vault_import_cancel();
    if (error == ESP_OK) prepare_preview(entries, count, preview);
    RETURN(error);
}
esp_err_t vault_otpauth_preview(const char *const *uris, size_t count, vault_import_preview_t *preview)
{
    vault_guard_enter();
    if (!vault_management_active()) RETURN(ESP_ERR_INVALID_STATE);
    vault_import_cancel();
    vault_entry_t *entries = count ? vault_try_alloc(count * sizeof(*entries)) : NULL;
    if (count && !entries) RETURN(ESP_ERR_NO_MEM);
    for (size_t i = 0; i < count; ++i) {
        esp_err_t error = vault_otpauth_read(uris[i], entries + i);
        if (error != ESP_OK) { clear_entries(entries, i + 1); RETURN(error); }
    }
    prepare_preview(entries, count, preview); RETURN(ESP_OK);
}
const vault_entry_t *vault_import_entry_at(size_t index) { return state.pending + index; }
esp_err_t vault_import_commit(void)
{
    vault_guard_enter();
    if (!vault_management_active() || !state.preview) RETURN(ESP_ERR_INVALID_STATE);
    size_t count = state.count + state.pending_count;
    vault_entry_t *next = count ? vault_alloc(count * sizeof(*next)) : NULL;
    if (state.count) memcpy(next, state.entries, state.count * sizeof(*next));
    if (state.pending_count) memcpy(next + state.count, state.pending, state.pending_count * sizeof(*next));
    catalog_t header = state.header;
    for (size_t i = state.count; i < count; ++i) next[i].id = header.next_id++;
    esp_err_t error = seal_catalog(next, count, state.key, &header);
    if (error == ESP_OK) {
        vault_clear_free(state.entries, state.count * sizeof(*next));
        vault_clear_free(state.pending, state.pending_count * sizeof(*next));
        state.entries = next; state.count = count; state.pending = NULL; state.pending_count = 0;
        state.preview = false; state.skipped = 0; vault_activity();
    } else vault_clear_free(next, count * sizeof(*next));
    RETURN(error);
}
