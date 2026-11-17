// SPDX-License-Identifier: AGPL-3.0-or-later
#include "unity.h"
#include "vault_private.h"
#include <stdio.h>
void setUp(void) { }
void tearDown(void) { }

static void test_rfc6238_vectors(void)
{
    // RFC 6238 Appendix B uses different key lengths for SHA1/256/512.
    static const uint8_t secret[] = "1234567890123456789012345678901234567890123456789012345678901234";
    static const uint64_t times[] = { 59, 1111111109, 1111111111, 1234567890, 2000000000, 20000000000 };
    static const char *codes[6][3] = {
        { "94287082", "46119246", "90693936" }, { "07081804", "68084774", "25091201" },
        { "14050471", "67062674", "99943326" }, { "89005924", "91819424", "93441116" },
        { "69279037", "90698825", "38618901" }, { "65353130", "77737706", "47863826" }
    };
    size_t lengths[] = { 20, 32, 64 };
    vault_entry_t entry = { .kind = VAULT_TOTP, .name = "RFC vector",
                           .data.totp = { .secret = secret, .digits = 8, .period = 30 } };
    for (size_t row = 0; row < 6; ++row) for (unsigned algorithm = 0; algorithm < 3; ++algorithm) {
        entry.data.totp.secret_len = lengths[algorithm]; entry.data.totp.algorithm = algorithm;
        char code[9]; uint32_t remaining;
        TEST_ASSERT_EQUAL(ESP_OK, vault_totp(&entry, (time_t)times[row], code, &remaining));
        TEST_ASSERT_EQUAL_STRING(codes[row][algorithm], code);
        TEST_ASSERT_EQUAL_UINT32(30 - times[row] % 30, remaining);
    }
}

static void test_entry_encryption_roundtrip(void)
{
    uint8_t hardware[32] = {1}, salt[16] = {2}, key[32], nonce[12] = {3};
    uint8_t aad[8] = {1, 0, 0, 0, 1, 0, 0, 0}, tag[16], cipher[24], plain[24];
    const uint8_t value[24] = "example secret value";
    TEST_ASSERT_EQUAL(ESP_OK, vault_crypto_key(hardware, "482913", 6, salt, 1000, key));
    TEST_ASSERT_EQUAL(ESP_OK, vault_crypto_seal(key, nonce, aad, 8, value, 24, cipher, tag));
    TEST_ASSERT_NOT_EQUAL(0, memcmp(value, cipher, 24));
    TEST_ASSERT_EQUAL(ESP_OK, vault_crypto_open(key, nonce, aad, 8, cipher, 24, tag, plain));
    TEST_ASSERT_EQUAL_MEMORY(value, plain, 24);
    aad[0] ^= 1;
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_CRC, vault_crypto_open(key, nonce, aad, 8, cipher, 24, tag, plain));
    uint8_t zero[24] = {0}; TEST_ASSERT_EQUAL_MEMORY(zero, plain, 24);
    mbedtls_platform_zeroize(key, sizeof(key));
}

static void test_backup_format_roundtrip(void)
{
    const char *json = "{\"ver\":1,\"entries\":[{\"type\":\"totp\",\"name\":\"\\u793a\\u4f8b\\uD83D\\uDD12\",\"secret_b32\":\"GEZDGNBVGY3TQOJQGEZDGNBVGY3TQOJQ\",\"digits\":8,\"period\":30,\"algo\":\"SHA1\"},{\"type\":\"pass\",\"name\":\"Example login\",\"user\":\"example\",\"pass\":\"<YOUR_TOKEN>\",\"url\":\"https://vault.example.com\",\"note\":\"line1\\nline2\\\"\"}]}";
    vault_entry_t *entries; size_t count;
    TEST_ASSERT_EQUAL(ESP_OK, vault_json_read(json, strlen(json), &entries, &count));
    TEST_ASSERT_EQUAL_UINT32(2, count);
    TEST_ASSERT_EQUAL_STRING("示例🔒", entries[0].name);
    TEST_ASSERT_EQUAL_STRING("line1\nline2\"", entries[1].data.password.note);
    size_t json_size; char *encoded = vault_json_write(entries, count, &json_size);
    uint8_t key[32] = {4}, salt[16] = {5}, nonce[12] = {6}, *backup, *restored;
    size_t backup_size, restored_size;
    TEST_ASSERT_EQUAL(ESP_OK, vault_backup_pack(key, salt, nonce, (uint8_t *)encoded, json_size, &backup, &backup_size));
    TEST_ASSERT_EQUAL_MEMORY("NVB1", backup, 4);
    TEST_ASSERT_EQUAL_UINT32(json_size + 55, backup_size);
    TEST_ASSERT_EQUAL(ESP_OK, vault_backup_open(key, backup, backup_size, &restored, &restored_size));
    TEST_ASSERT_EQUAL_MEMORY(encoded, restored, json_size);
    vault_entry_t *roundtrip; size_t roundtrip_count;
    TEST_ASSERT_EQUAL(ESP_OK, vault_json_read((char *)restored, restored_size, &roundtrip, &roundtrip_count));
    TEST_ASSERT_EQUAL_STRING(entries[1].data.password.password, roundtrip[1].data.password.password);
    backup[5] ^= 1;
    uint8_t *rejected; size_t rejected_size;
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, vault_backup_open(key, backup, backup_size, &rejected, &rejected_size));
    backup[5] ^= 1; backup[backup_size - 1] ^= 1;
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_CRC, vault_backup_open(key, backup, backup_size, &rejected, &rejected_size));
    for (size_t i = 0; i < count; ++i) { vault_entry_clear(entries + i); vault_entry_clear(roundtrip + i); }
    vault_clear_free(entries, count * sizeof(*entries)); vault_clear_free(roundtrip, count * sizeof(*roundtrip));
    vault_clear_free(encoded, json_size + 1); vault_clear_free(restored, restored_size + 1); free(backup);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_rfc6238_vectors);
    RUN_TEST(test_entry_encryption_roundtrip);
    RUN_TEST(test_backup_format_roundtrip);
    return UNITY_END();
}
