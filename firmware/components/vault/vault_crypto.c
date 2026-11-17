// SPDX-License-Identifier: AGPL-3.0-or-later
#include "vault_private.h"
#include <stdio.h>
#include "mbedtls/gcm.h"
#include "mbedtls/hkdf.h"
#include "mbedtls/md.h"
#include "mbedtls/pkcs5.h"
#if defined(ESP_PLATFORM) && !CONFIG_MBEDTLS_INTERNAL_MEM_ALLOC
#error "Vault requires internal-only mbedTLS allocations"
#endif

esp_err_t vault_crypto_key(const uint8_t hardware[32], const char *pin, size_t pin_size,
                          const uint8_t salt[16], uint32_t iterations, uint8_t key[32])
{
    uint8_t material[64];
    memcpy(material, hardware, 32);
    int result = mbedtls_pkcs5_pbkdf2_hmac_ext(MBEDTLS_MD_SHA256, (const uint8_t *)pin,
                                             pin_size, salt, 16, iterations, 32, material + 32);
    if (!result)
        result = mbedtls_hkdf(mbedtls_md_info_from_type(MBEDTLS_MD_SHA256), NULL, 0,
                              material, 64, (const uint8_t *)"vault", 5, key, 32);
    mbedtls_platform_zeroize(material, sizeof(material));
    return result ? ESP_FAIL : ESP_OK;
}

esp_err_t vault_crypto_seal(const uint8_t key[32], const uint8_t nonce[12],
                           const uint8_t *aad, size_t aad_size, const uint8_t *plain,
                           size_t size, uint8_t *cipher, uint8_t tag[16])
{
    mbedtls_gcm_context context;
    mbedtls_gcm_init(&context);
    int result = mbedtls_gcm_setkey(&context, MBEDTLS_CIPHER_ID_AES, key, 256);
    if (!result)
        result = mbedtls_gcm_crypt_and_tag(&context, MBEDTLS_GCM_ENCRYPT, size, nonce, 12,
                                          aad, aad_size, plain, cipher, 16, tag);
    mbedtls_gcm_free(&context);
    mbedtls_platform_zeroize(&context, sizeof(context));
    return result ? ESP_FAIL : ESP_OK;
}

esp_err_t vault_crypto_open(const uint8_t key[32], const uint8_t nonce[12],
                           const uint8_t *aad, size_t aad_size, const uint8_t *cipher,
                           size_t size, const uint8_t tag[16], uint8_t *plain)
{
    mbedtls_gcm_context context;
    mbedtls_gcm_init(&context);
    int result = mbedtls_gcm_setkey(&context, MBEDTLS_CIPHER_ID_AES, key, 256);
    if (!result)
        result = mbedtls_gcm_auth_decrypt(&context, size, nonce, 12, aad, aad_size,
                                         tag, 16, cipher, plain);
    mbedtls_gcm_free(&context);
    mbedtls_platform_zeroize(&context, sizeof(context));
    if (result) mbedtls_platform_zeroize(plain, size);
    return result ? ESP_ERR_INVALID_CRC : ESP_OK;
}

esp_err_t vault_totp(const vault_entry_t *entry, time_t now, char code[9], uint32_t *remaining)
{
    static const mbedtls_md_type_t algorithms[] = { MBEDTLS_MD_SHA1, MBEDTLS_MD_SHA256, MBEDTLS_MD_SHA512 };
    const mbedtls_md_info_t *algorithm = mbedtls_md_info_from_type(algorithms[entry->data.totp.algorithm]);
    uint64_t counter = (uint64_t)now / entry->data.totp.period;
    uint8_t message[8], digest[64];
    for (unsigned i = 0; i < 8; ++i) message[7 - i] = counter >> (i * 8);
    int result = mbedtls_md_hmac(algorithm, entry->data.totp.secret,
                                entry->data.totp.secret_len, message, 8, digest);
    if (result) return ESP_FAIL;
    unsigned offset = digest[mbedtls_md_get_size(algorithm) - 1] & 15;
    uint32_t value = ((uint32_t)(digest[offset] & 127) << 24) |
                     ((uint32_t)digest[offset + 1] << 16) |
                     ((uint32_t)digest[offset + 2] << 8) | digest[offset + 3];
    value %= entry->data.totp.digits == 8 ? 100000000 : 1000000;
    snprintf(code, 9, entry->data.totp.digits == 8 ? "%08u" : "%06u", (unsigned)value);
    *remaining = entry->data.totp.period - (uint64_t)now % entry->data.totp.period;
    mbedtls_platform_zeroize(digest, sizeof(digest));
    mbedtls_platform_zeroize(message, sizeof(message));
    return ESP_OK;
}

static const uint8_t backup_profile[11] = { 'N', 'V', 'B', '1', 1, 0, 128, 0, 0, 8, 1 };

esp_err_t vault_backup_pack(const uint8_t key[32], const uint8_t salt[16],
                           const uint8_t nonce[12], const uint8_t *plain, size_t plain_size,
                           uint8_t **encrypted, size_t *length)
{
    *length = 55 + plain_size;
    uint8_t *buffer = malloc(*length);
    if (!buffer) return ESP_ERR_NO_MEM;
    memcpy(buffer, backup_profile, 11);
    memcpy(buffer + 11, salt, 16);
    memcpy(buffer + 27, nonce, 12);
    esp_err_t error = vault_crypto_seal(key, nonce, NULL, 0, plain, plain_size,
                                       buffer + 39, buffer + 39 + plain_size);
    if (error != ESP_OK) { free(buffer); return error; }
    *encrypted = buffer;
    return ESP_OK;
}

esp_err_t vault_backup_open(const uint8_t key[32], const uint8_t *encrypted,
                           size_t size, uint8_t **plain, size_t *plain_size)
{
    if (size < 55 || memcmp(encrypted, backup_profile, 11)) return ESP_ERR_INVALID_ARG;
    *plain_size = size - 55;
    uint8_t *buffer = vault_try_alloc(*plain_size + 1);
    if (!buffer) return ESP_ERR_NO_MEM;
    esp_err_t error = vault_crypto_open(key, encrypted + 27, NULL, 0,
                                       encrypted + 39, *plain_size, encrypted + size - 16, buffer);
    if (error != ESP_OK) { vault_clear_free(buffer, *plain_size + 1); return error; }
    *plain = buffer;
    return ESP_OK;
}
