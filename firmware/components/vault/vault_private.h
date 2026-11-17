// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "vault.h"
#include <stdlib.h>
#include <string.h>
#include "mbedtls/platform_util.h"
#ifdef ESP_PLATFORM
#include "esp_attr.h"
#include "esp_heap_caps.h"
#include "esp_random.h"
#include "esp_timer.h"
#define VAULT_INTERNAL DRAM_ATTR
#else
#include <assert.h>
#define VAULT_INTERNAL
#endif

static inline void *vault_try_alloc(size_t size)
{
#ifdef ESP_PLATFORM
    return heap_caps_calloc(1, size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
#else
    return calloc(1, size);
#endif
}
static inline void *vault_alloc(size_t size)
{
#ifdef ESP_PLATFORM
    void *memory = heap_caps_calloc(1, size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    ESP_ERROR_CHECK(memory ? ESP_OK : ESP_ERR_NO_MEM);
#else
    void *memory = calloc(1, size);
    assert(memory);
#endif
    return memory;
}
static inline void vault_clear_free(void *memory, size_t size)
{
    if (memory) mbedtls_platform_zeroize(memory, size);
    free(memory);
}
static inline char *vault_string(const char *text)
{
    size_t length = strlen(text) + 1;
    char *copy = vault_alloc(length);
    memcpy(copy, text, length);
    return copy;
}
void vault_entry_clear(vault_entry_t *entry);
vault_entry_t vault_entry_copy(const vault_entry_t *entry);
esp_err_t vault_json_read(const char *json, size_t length, vault_entry_t **entries, size_t *count);
char *vault_json_write(const vault_entry_t *entries, size_t count, size_t *length);
esp_err_t vault_otpauth_read(const char *uri, vault_entry_t *entry);
esp_err_t vault_base32_decode(const char *text, uint8_t **data, size_t *length);
bool vault_utf8(const char *text, size_t length);
esp_err_t vault_crypto_key(const uint8_t hardware[32], const char *pin, size_t pin_size,
                          const uint8_t salt[16], uint32_t iterations, uint8_t key[32]);
esp_err_t vault_crypto_seal(const uint8_t key[32], const uint8_t nonce[12],
                           const uint8_t *aad, size_t aad_size, const uint8_t *plain,
                           size_t size, uint8_t *cipher, uint8_t tag[16]);
esp_err_t vault_crypto_open(const uint8_t key[32], const uint8_t nonce[12],
                           const uint8_t *aad, size_t aad_size, const uint8_t *cipher,
                           size_t size, const uint8_t tag[16], uint8_t *plain);
esp_err_t vault_backup_pack(const uint8_t key[32], const uint8_t salt[16],
                           const uint8_t nonce[12], const uint8_t *plain, size_t plain_size,
                           uint8_t **encrypted, size_t *length);
esp_err_t vault_backup_open(const uint8_t key[32], const uint8_t *encrypted,
                           size_t size, uint8_t **plain, size_t *plain_size);
