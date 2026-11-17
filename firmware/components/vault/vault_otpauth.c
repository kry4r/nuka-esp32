// SPDX-License-Identifier: AGPL-3.0-or-later
#include "vault_private.h"
#include <ctype.h>
#include <limits.h>

static int hex_digit(unsigned char value)
{
    if (value >= '0' && value <= '9') return value - '0';
    value |= 32;
    return value >= 'a' && value <= 'f' ? value - 'a' + 10 : -1;
}
static char *url_decode(const char *text, size_t size)
{
    char *output = vault_try_alloc(size + 1); size_t used = 0;
    if (!output) return NULL;
    for (size_t i = 0; i < size; ++i) {
        unsigned char value = text[i];
        if (value == '%') {
            if (i + 2 >= size) goto invalid;
            int high = hex_digit(text[i + 1]), low = hex_digit(text[i + 2]);
            if (high < 0 || low < 0 || !(high | low)) goto invalid;
            value = high * 16 + low; i += 2;
        }
        output[used++] = value;
    }
    if (!vault_utf8(output, used)) goto invalid;
    return output;
invalid:
    vault_clear_free(output, size + 1); return NULL;
}
static bool positive_integer(const char *text, uint32_t *value)
{
    if (!*text) return false;
    uint64_t number = 0;
    for (; *text; ++text) {
        if (!isdigit((unsigned char)*text)) return false;
        number = number * 10 + *text - '0';
        if (number > UINT32_MAX) return false;
    }
    *value = number; return number > 0;
}
esp_err_t vault_otpauth_read(const char *uri, vault_entry_t *entry)
{
    static const char prefix[] = "otpauth://totp/";
    if (strncmp(uri, prefix, sizeof(prefix) - 1)) return ESP_ERR_INVALID_ARG;
    const char *label = uri + sizeof(prefix) - 1, *query = strchr(label, '?');
    if (!query || query == label || strchr(query, '#')) return ESP_ERR_INVALID_ARG;
    entry->kind = VAULT_TOTP;
    entry->name = url_decode(label, query - label);
    if (!entry->name) return ESP_ERR_INVALID_ARG;
    entry->data.totp.digits = 6; entry->data.totp.period = 30; entry->data.totp.algorithm = VAULT_SHA1;
    unsigned seen = 0;
    for (const char *at = query + 1; *at;) {
        const char *end = strchr(at, '&'); if (!end) end = at + strlen(at);
        const char *equals = memchr(at, '=', end - at);
        if (!equals) return ESP_ERR_INVALID_ARG;
        char *key = url_decode(at, equals - at), *value = url_decode(equals + 1, end - equals - 1);
        if (!key || !value) {
            vault_clear_free(key, key ? strlen(key) + 1 : 0);
            vault_clear_free(value, value ? strlen(value) + 1 : 0); return ESP_ERR_INVALID_ARG;
        }
        esp_err_t error = ESP_OK; unsigned flag = 0;
        if (!strcmp(key, "secret")) {
            flag = 1;
            if (!(seen & flag)) error = vault_base32_decode(value, (uint8_t **)&entry->data.totp.secret, &entry->data.totp.secret_len);
        } else if (!strcmp(key, "digits")) {
            flag = 2; uint32_t digits;
            if (!positive_integer(value, &digits) || (digits != 6 && digits != 8)) error = ESP_ERR_INVALID_ARG;
            else entry->data.totp.digits = digits;
        } else if (!strcmp(key, "period")) {
            flag = 4;
            if (!positive_integer(value, &entry->data.totp.period)) error = ESP_ERR_INVALID_ARG;
        } else if (!strcmp(key, "algorithm")) {
            flag = 8;
            if (!strcmp(value, "SHA1")) entry->data.totp.algorithm = VAULT_SHA1;
            else if (!strcmp(value, "SHA256")) entry->data.totp.algorithm = VAULT_SHA256;
            else if (!strcmp(value, "SHA512")) entry->data.totp.algorithm = VAULT_SHA512;
            else error = ESP_ERR_INVALID_ARG;
        }
        if (seen & flag) error = ESP_ERR_INVALID_ARG;
        seen |= flag;
        vault_clear_free(key, strlen(key) + 1); vault_clear_free(value, strlen(value) + 1);
        if (error != ESP_OK) return error;
        at = *end ? end + 1 : end;
    }
    return seen & 1 ? ESP_OK : ESP_ERR_INVALID_ARG;
}
