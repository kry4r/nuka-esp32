// SPDX-License-Identifier: AGPL-3.0-or-later
#include "vault_private.h"
#include "vault_json.h"
#include <ctype.h>
#include <limits.h>
#include <stdio.h>
#define JSMN_STATIC
#define JSMN_STRICT
#define JSMN_PARENT_LINKS
#include "vendor/jsmn.h"

struct vault_json_document { const char *text; jsmntok_t *tokens; int count; };
typedef struct vault_json_document document_t;
typedef struct { char *data; size_t size, capacity; } writer_t;
static char *decoded_string(const document_t *document, int token);

bool vault_utf8(const char *text, size_t length)
{
    for (size_t i = 0; i < length;) {
        unsigned byte = (unsigned char)text[i++];
        if (byte < 128) continue;
        unsigned needed, value, minimum;
        if (byte >= 0xC2 && byte <= 0xDF) { needed = 1; value = byte & 31; minimum = 0x80; }
        else if (byte >= 0xE0 && byte <= 0xEF) { needed = 2; value = byte & 15; minimum = 0x800; }
        else if (byte >= 0xF0 && byte <= 0xF4) { needed = 3; value = byte & 7; minimum = 0x10000; }
        else return false;
        if (length - i < needed) return false;
        while (needed--) {
            byte = (unsigned char)text[i++];
            if ((byte & 0xC0) != 0x80) return false;
            value = (value << 6) | (byte & 63);
        }
        if (value < minimum || value > 0x10FFFF || (value >= 0xD800 && value <= 0xDFFF)) return false;
    }
    return true;
}

static int token_after(const document_t *document, int token)
{
    int end = document->tokens[token].end;
    do { ++token; } while (token < document->count && document->tokens[token].start < end);
    return token;
}

static int string_equal(const document_t *document, int token, const char *value)
{
    char *text = decoded_string(document, token);
    bool equal = text && !strcmp(text, value);
    vault_clear_free(text, text ? strlen(text) + 1 : 0);
    return equal;
}

static int field(const document_t *document, int object, const char *name)
{
    if (object < 0 || document->tokens[object].type != JSMN_OBJECT) return -1;
    for (int key = object + 1; key < token_after(document, object);) {
        int value = key + 1;
        if (string_equal(document, key, name)) return value;
        key = token_after(document, value);
    }
    return -1;
}

static bool number_valid(const char *text, size_t length)
{
    size_t at = 0;
    if (at < length && text[at] == '-') ++at;
    if (at == length) return false;
    if (text[at] == '0') ++at;
    else {
        if (text[at] < '1' || text[at] > '9') return false;
        while (at < length && isdigit((unsigned char)text[at])) ++at;
    }
    if (at < length && text[at] == '.') {
        size_t first = ++at;
        while (at < length && isdigit((unsigned char)text[at])) ++at;
        if (at == first) return false;
    }
    if (at < length && (text[at] == 'e' || text[at] == 'E')) {
        ++at;
        if (at < length && (text[at] == '+' || text[at] == '-')) ++at;
        size_t first = at;
        while (at < length && isdigit((unsigned char)text[at])) ++at;
        if (at == first) return false;
    }
    return at == length;
}

static bool whitespace(const char *text, size_t begin, size_t end)
{
    while (begin < end) {
        char c = text[begin++];
        if (c != ' ' && c != '\t' && c != '\n' && c != '\r') return false;
    }
    return true;
}

static size_t token_begin(const jsmntok_t *token) { return token->start - (token->type == JSMN_STRING); }
static size_t token_end(const jsmntok_t *token) { return token->end + (token->type == JSMN_STRING); }

static bool delimiter(const char *text, size_t begin, size_t end, char expected)
{
    while (begin < end && isspace((unsigned char)text[begin])) ++begin;
    return begin < end && text[begin++] == expected && whitespace(text, begin, end);
}

static bool syntax_valid(const document_t *document, size_t length)
{
    if (token_after(document, 0) != document->count ||
        !whitespace(document->text, 0, token_begin(document->tokens)) ||
        !whitespace(document->text, token_end(document->tokens), length)) return false;
    for (int i = 0; i < document->count; ++i) {
        const jsmntok_t *token = document->tokens + i;
        if (token->type == JSMN_STRING) {
            for (int at = token->start; at < token->end; ++at)
                if ((unsigned char)document->text[at] < 32) return false;
        } else if (token->type == JSMN_PRIMITIVE) {
            const char *text = document->text + token->start;
            size_t size = token->end - token->start;
            if (!((size == 4 && (!memcmp(text, "true", 4) || !memcmp(text, "null", 4))) ||
                  (size == 5 && !memcmp(text, "false", 5)) || number_valid(text, size))) return false;
        } else {
            int next = i + 1;
            size_t cursor = token->start + 1;
            for (int child = 0; child < token->size; ++child) {
                if (next >= document->count || document->tokens[next].start >= token->end) return false;
                if (token->type == JSMN_OBJECT) {
                    if (document->tokens[next].type != JSMN_STRING ||
                        (child ? !delimiter(document->text, cursor, token_begin(document->tokens + next), ',') :
                                 !whitespace(document->text, cursor, token_begin(document->tokens + next)))) return false;
                    cursor = token_end(document->tokens + next++);
                    if (next >= document->count || document->tokens[next].start >= token->end) return false;
                    if (!delimiter(document->text, cursor, token_begin(document->tokens + next), ':')) return false;
                } else if (child ? !delimiter(document->text, cursor, token_begin(document->tokens + next), ',') :
                                    !whitespace(document->text, cursor, token_begin(document->tokens + next))) return false;
                cursor = token_end(document->tokens + next);
                next = token_after(document, next);
            }
            if (!whitespace(document->text, cursor, token->end - 1)) return false;
        }
    }
    return true;
}

static unsigned hex4(const char *text)
{
    unsigned value = 0;
    for (unsigned i = 0; i < 4; ++i) value = value * 16 + (text[i] <= '9' ? text[i] - '0' : (text[i] | 32) - 'a' + 10);
    return value;
}

static char *decoded_string(const document_t *document, int token)
{
    if (token < 0 || document->tokens[token].type != JSMN_STRING) return NULL;
    const jsmntok_t *item = document->tokens + token;
    char *output = vault_try_alloc(item->end - item->start + 1);
    if (!output) return NULL;
    size_t length = 0;
    for (int at = item->start; at < item->end; ++at) {
        unsigned value = (unsigned char)document->text[at];
        if (value == '\\') {
            value = document->text[++at];
            if (value == 'u') {
                value = hex4(document->text + at + 1); at += 4;
                if (value >= 0xD800 && value <= 0xDBFF) {
                    if (at + 6 >= item->end || document->text[at + 1] != '\\' || document->text[at + 2] != 'u') goto invalid;
                    unsigned low = hex4(document->text + at + 3);
                    if (low < 0xDC00 || low > 0xDFFF) goto invalid;
                    value = 0x10000 + ((value - 0xD800) << 10) + low - 0xDC00; at += 6;
                } else if (value >= 0xDC00 && value <= 0xDFFF) goto invalid;
                if (!value) goto invalid;
                if (value >= 0x10000) output[length++] = 0xF0 | (value >> 18);
                if (value >= 0x800) output[length++] = (value >= 0x10000 ? 0x80 : 0xE0) | ((value >> 12) & 63);
                if (value >= 0x80) output[length++] = (value >= 0x800 ? 0x80 : 0xC0) | ((value >> 6) & 63);
                output[length++] = value < 0x80 ? value : 0x80 | (value & 63);
                continue;
            }
            const char *escape = "\"\\/bfnrt", *mapped = "\"\\/\b\f\n\r\t";
            value = mapped[strchr(escape, value) - escape];
        }
        output[length++] = value;
    }
    return output;
invalid:
    vault_clear_free(output, item->end - item->start + 1);
    return NULL;
}

static bool integer(const document_t *document, int token, uint32_t *value)
{
    if (token < 0 || document->tokens[token].type != JSMN_PRIMITIVE) return false;
    uint64_t number = 0;
    const jsmntok_t *item = document->tokens + token;
    for (int i = item->start; i < item->end; ++i) {
        if (!isdigit((unsigned char)document->text[i])) return false;
        number = number * 10 + document->text[i] - '0';
        if (number > UINT32_MAX) return false;
    }
    *value = number;
    return true;
}

esp_err_t vault_json_open(const char *json, size_t length, vault_json_document_t **output)
{
    if (!vault_utf8(json, length)) return ESP_ERR_INVALID_ARG;
    jsmn_parser parser; jsmn_init(&parser);
    int needed = jsmn_parse(&parser, json, length, NULL, 0);
    if (needed < 1) return ESP_ERR_INVALID_ARG;
    document_t *document = vault_try_alloc(sizeof(*document));
    if (!document) return ESP_ERR_NO_MEM;
    *document = (document_t){ .text = json, .tokens = vault_try_alloc(needed * sizeof(jsmntok_t)), .count = needed };
    if (!document->tokens) { vault_clear_free(document, sizeof(*document)); return ESP_ERR_NO_MEM; }
    jsmn_init(&parser);
    int parsed = jsmn_parse(&parser, json, length, document->tokens, needed);
    if (parsed != needed || document->tokens[0].type != JSMN_OBJECT || !syntax_valid(document, length)) {
        vault_json_close(document); return ESP_ERR_INVALID_ARG;
    }
    *output = document; return ESP_OK;
}
void vault_json_close(vault_json_document_t *document)
{
    vault_clear_free(document->tokens, document->count * sizeof(jsmntok_t));
    vault_clear_free(document, sizeof(*document));
}
bool vault_json_object(const vault_json_document_t *document, int token)
{ return token >= 0 && document->tokens[token].type == JSMN_OBJECT; }
bool vault_json_fields(const vault_json_document_t *document, int object, const char *allowed)
{
    if (!vault_json_object(document, object)) return false;
    for (int key = object + 1; key < token_after(document, object); key = token_after(document, key + 1)) {
        char *name = decoded_string(document, key);
        if (!name) return false;
        bool found = false;
        size_t length = strlen(name);
        for (const char *at = allowed; *at;) {
            const char *end = strchr(at, '|');
            size_t size = end ? (size_t)(end - at) : strlen(at);
            if (size == length && !memcmp(at, name, length)) found = true;
            if (!end) break;
            at = end + 1;
        }
        for (int prior = object + 1; found && prior < key; prior = token_after(document, prior + 1)) {
            char *other = decoded_string(document, prior);
            if (!other || !strcmp(other, name)) found = false;
            vault_clear_free(other, other ? strlen(other) + 1 : 0);
        }
        vault_clear_free(name, length + 1);
        if (!found) return false;
    }
    return true;
}
int vault_json_field(const vault_json_document_t *document, int object, const char *name)
{ return field(document, object, name); }
int vault_json_array_size(const vault_json_document_t *document, int array)
{ return array < 0 || document->tokens[array].type != JSMN_ARRAY ? -1 : document->tokens[array].size; }
int vault_json_array_at(const vault_json_document_t *document, int array, size_t index)
{
    int count = vault_json_array_size(document, array);
    if (count < 0 || index >= (size_t)count) return -1;
    int token = array + 1;
    while (index--) token = token_after(document, token);
    return token;
}
bool vault_json_u32(const vault_json_document_t *document, int token, uint32_t *value)
{ return integer(document, token, value); }
bool vault_json_bool(const vault_json_document_t *document, int token, bool *value)
{
    if (token < 0 || document->tokens[token].type != JSMN_PRIMITIVE) return false;
    const jsmntok_t *item = document->tokens + token;
    if (item->end - item->start == 4 && !memcmp(document->text + item->start, "true", 4)) *value = true;
    else if (item->end - item->start == 5 && !memcmp(document->text + item->start, "false", 5)) *value = false;
    else return false;
    return true;
}
char *vault_json_string(const vault_json_document_t *document, int token)
{ return decoded_string(document, token); }
char *vault_json_raw(const vault_json_document_t *document, int token)
{
    if (token < 0) return NULL;
    size_t begin = token_begin(document->tokens + token), end = token_end(document->tokens + token);
    char *text = vault_try_alloc(end - begin + 1);
    if (!text) return NULL;
    memcpy(text, document->text + begin, end - begin); return text;
}

esp_err_t vault_base32_decode(const char *text, uint8_t **data, size_t *length)
{
    size_t size = strlen(text), used = 0;
    uint8_t *bytes = vault_try_alloc(size * 5 / 8 + 1);
    if (!bytes) return ESP_ERR_NO_MEM;
    unsigned accumulator = 0, bits = 0;
    unsigned padding = 0;
    for (size_t i = 0; i < size; ++i) {
        unsigned char c = text[i];
        if (c == '=') { if (++padding > 6) goto invalid; continue; }
        int value = c >= 'A' && c <= 'Z' ? c - 'A' : c >= '2' && c <= '7' ? c - '2' + 26 : -1;
        if (value < 0 || padding) { vault_clear_free(bytes, size * 5 / 8 + 1); return ESP_ERR_INVALID_ARG; }
        accumulator = (accumulator << 5) | value; bits += 5;
        if (bits >= 8) { bits -= 8; bytes[used++] = accumulator >> bits; }
    }
    if (!used || bits >= 5 || (accumulator & ((1u << bits) - 1))) goto invalid;
    *data = bytes; *length = used; return ESP_OK;
invalid:
    vault_clear_free(bytes, size * 5 / 8 + 1); return ESP_ERR_INVALID_ARG;
}

void vault_entry_clear(vault_entry_t *entry)
{
    vault_clear_free((void *)entry->name, entry->name ? strlen(entry->name) : 0);
    if (entry->kind == VAULT_TOTP) vault_clear_free((void *)entry->data.totp.secret, entry->data.totp.secret_len);
    else {
        const char *fields[] = { entry->data.password.user, entry->data.password.password, entry->data.password.url, entry->data.password.note };
        for (unsigned i = 0; i < 4; ++i) vault_clear_free((void *)fields[i], fields[i] ? strlen(fields[i]) : 0);
    }
    mbedtls_platform_zeroize(entry, sizeof(*entry));
}

vault_entry_t vault_entry_copy(const vault_entry_t *entry)
{
    vault_entry_t copy = *entry;
    copy.name = vault_string(entry->name);
    if (entry->kind == VAULT_TOTP) {
        uint8_t *secret = vault_alloc(entry->data.totp.secret_len);
        memcpy(secret, entry->data.totp.secret, entry->data.totp.secret_len); copy.data.totp.secret = secret;
    } else {
        copy.data.password.user = vault_string(entry->data.password.user);
        copy.data.password.password = vault_string(entry->data.password.password);
        copy.data.password.url = vault_string(entry->data.password.url);
        copy.data.password.note = vault_string(entry->data.password.note);
    }
    return copy;
}

static esp_err_t read_entry(const document_t *document, int object, vault_entry_t *entry)
{
    if (document->tokens[object].type != JSMN_OBJECT) return ESP_ERR_INVALID_ARG;
    entry->name = decoded_string(document, field(document, object, "name"));
    int type = field(document, object, "type");
    if (type < 0 || !entry->name) return ESP_ERR_INVALID_ARG;
    if (string_equal(document, type, "totp")) {
        entry->kind = VAULT_TOTP;
        char *secret = decoded_string(document, field(document, object, "secret_b32"));
        uint32_t digits, period;
        int algorithm = field(document, object, "algo");
        if (!secret) return ESP_ERR_INVALID_ARG;
        esp_err_t error = vault_base32_decode(secret, (uint8_t **)&entry->data.totp.secret, &entry->data.totp.secret_len);
        vault_clear_free(secret, strlen(secret));
        if (error != ESP_OK || !integer(document, field(document, object, "digits"), &digits) ||
            (digits != 6 && digits != 8) || !integer(document, field(document, object, "period"), &period) || !period || algorithm < 0)
            return ESP_ERR_INVALID_ARG;
        if (string_equal(document, algorithm, "SHA1")) entry->data.totp.algorithm = VAULT_SHA1;
        else if (string_equal(document, algorithm, "SHA256")) entry->data.totp.algorithm = VAULT_SHA256;
        else if (string_equal(document, algorithm, "SHA512")) entry->data.totp.algorithm = VAULT_SHA512;
        else return ESP_ERR_INVALID_ARG;
        entry->data.totp.digits = digits; entry->data.totp.period = period;
    } else if (string_equal(document, type, "pass")) {
        entry->kind = VAULT_PASSWORD;
        entry->data.password.user = decoded_string(document, field(document, object, "user"));
        entry->data.password.password = decoded_string(document, field(document, object, "pass"));
        entry->data.password.url = decoded_string(document, field(document, object, "url"));
        int note = field(document, object, "note");
        entry->data.password.note = note < 0 ? vault_string("") : decoded_string(document, note);
        if (!entry->data.password.user || !entry->data.password.password || !entry->data.password.url || !entry->data.password.note)
            return ESP_ERR_INVALID_ARG;
    } else return ESP_ERR_INVALID_ARG;
    return ESP_OK;
}

esp_err_t vault_json_read(const char *json, size_t length, vault_entry_t **entries, size_t *count)
{
    document_t *document;
    esp_err_t opened = vault_json_open(json, length, &document);
    if (opened != ESP_OK) return opened;
    esp_err_t error = ESP_ERR_INVALID_ARG;
    vault_entry_t *result = NULL; size_t total = 0, initialized = 0;
    uint32_t version;
    if (!integer(document, field(document, 0, "ver"), &version) || version != 1) goto done;
    int array = field(document, 0, "entries");
    if (array < 0 || document->tokens[array].type != JSMN_ARRAY) goto done;
    total = document->tokens[array].size;
    if (total) {
        result = vault_try_alloc(total * sizeof(*result));
        if (!result) { error = ESP_ERR_NO_MEM; goto done; }
    }
    int token = array + 1;
    while (initialized < total) {
        error = read_entry(document, token, result + initialized++);
        if (error != ESP_OK) goto done;
        token = token_after(document, token);
    }
    *entries = result; *count = total; error = ESP_OK;
done:
    vault_json_close(document);
    if (error != ESP_OK) {
        for (size_t i = 0; i < initialized; ++i) vault_entry_clear(result + i);
        vault_clear_free(result, total * sizeof(*result));
    }
    return error;
}

static void append(writer_t *writer, const char *text, size_t length)
{
    if (writer->size + length + 1 > writer->capacity) {
        size_t capacity = (writer->size + length + 1) * 2;
        char *next = vault_alloc(capacity);
        if (writer->size) memcpy(next, writer->data, writer->size);
        vault_clear_free(writer->data, writer->capacity);
        writer->data = next; writer->capacity = capacity;
    }
    memcpy(writer->data + writer->size, text, length); writer->size += length;
    writer->data[writer->size] = 0;
}
static void literal(writer_t *writer, const char *text) { append(writer, text, strlen(text)); }
static void quoted(writer_t *writer, const char *text)
{
    literal(writer, "\"");
    for (const unsigned char *at = (const unsigned char *)text; *at; ++at) {
        if (*at == '"' || *at == '\\') { char escape[2] = { '\\', *at }; append(writer, escape, 2); }
        else if (*at < 32) { char escape[7]; snprintf(escape, sizeof(escape), "\\u%04x", *at); append(writer, escape, 6); }
        else append(writer, (const char *)at, 1);
    }
    literal(writer, "\"");
}
static void encode_base32(writer_t *writer, const uint8_t *bytes, size_t length)
{
    const char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ234567";
    unsigned accumulator = 0, bits = 0;
    for (size_t i = 0; i < length; ++i) {
        accumulator = (accumulator << 8) | bytes[i]; bits += 8;
        while (bits >= 5) { bits -= 5; append(writer, alphabet + ((accumulator >> bits) & 31), 1); }
    }
    if (bits) append(writer, alphabet + ((accumulator << (5 - bits)) & 31), 1);
}

char *vault_json_write(const vault_entry_t *entries, size_t count, size_t *length)
{
    writer_t writer = { 0 };
    literal(&writer, "{\"ver\":1,\"entries\":[");
    for (size_t i = 0; i < count; ++i) {
        const vault_entry_t *entry = entries + i;
        if (i) literal(&writer, ",");
        literal(&writer, entry->kind == VAULT_TOTP ? "{\"type\":\"totp\",\"name\":" : "{\"type\":\"pass\",\"name\":");
        quoted(&writer, entry->name);
        if (entry->kind == VAULT_TOTP) {
            literal(&writer, ",\"secret_b32\":\""); encode_base32(&writer, entry->data.totp.secret, entry->data.totp.secret_len);
            char fields[100];
            const char *algorithms[] = { "SHA1", "SHA256", "SHA512" };
            snprintf(fields, sizeof(fields), "\",\"digits\":%u,\"period\":%lu,\"algo\":\"%s\"}",
                     entry->data.totp.digits, (unsigned long)entry->data.totp.period, algorithms[entry->data.totp.algorithm]);
            literal(&writer, fields);
        } else {
            literal(&writer, ",\"user\":"); quoted(&writer, entry->data.password.user);
            literal(&writer, ",\"pass\":"); quoted(&writer, entry->data.password.password);
            literal(&writer, ",\"url\":"); quoted(&writer, entry->data.password.url);
            literal(&writer, ",\"note\":"); quoted(&writer, entry->data.password.note); literal(&writer, "}");
        }
    }
    literal(&writer, "]}");
    *length = writer.size;
    char *result = vault_alloc(writer.size + 1);
    memcpy(result, writer.data, writer.size + 1);
    vault_clear_free(writer.data, writer.capacity);
    return result;
}
