// SPDX-License-Identifier: AGPL-3.0-or-later
#include "proto.h"
#include <ctype.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
static bool integer(const cJSON *v) { return cJSON_IsNumber(v) && isfinite(v->valuedouble) && floor(v->valuedouble) == v->valuedouble; }
static unsigned utf8_length(const char *s)
{
    unsigned n = 0; for (const unsigned char *p = (const unsigned char *)s; *p; ++p) if ((*p & 0xc0) != 0x80) ++n;
    return n;
}
static bool equal_literal(const cJSON *value, const char *text)
{
    cJSON *literal = cJSON_Parse(text); bool same = cJSON_Compare(value, literal, true); cJSON_Delete(literal); return same;
}
static bool date_valid(const char *text)
{
    if(strlen(text)!=10||text[4]!='-'||text[7]!='-')return false;
    for(unsigned i=0;i<10;++i)if(i!=4&&i!=7&&!isdigit((unsigned char)text[i]))return false;
    unsigned year=(text[0]-'0')*1000+(text[1]-'0')*100+(text[2]-'0')*10+text[3]-'0';
    unsigned month=(text[5]-'0')*10+text[6]-'0',day=(text[8]-'0')*10+text[9]-'0';
    if(month<1||month>12)return false;
    unsigned days=(unsigned[]){31,28,31,30,31,30,31,31,30,31,30,31}[month-1];
    if(month==2&&year%4==0&&(year%100!=0||year%400==0))++days;
    return day>=1&&day<=days;
}
static bool matches(const char *text, const char *pattern)
{
    size_t n = strlen(text);
    if (!strcmp(pattern, "[^a-z0-9_-]")) {
        for (const unsigned char *p = (const unsigned char *)text; *p; ++p)
            if (!(*p >= 'a' && *p <= 'z') && !(*p >= '0' && *p <= '9') && *p != '_' && *p != '-') return true;
        return false;
    }
    if (!strcmp(pattern, "[^\\x20-\\x7e]")) {
        for (const unsigned char *p = (const unsigned char *)text; *p; ++p) if (*p < 32 || *p > 126) return true;
        return false;
    }
    if (strcmp(pattern, "^[a-z0-9_-]{1,32}$") == 0) {
        if (n == 0 || n > 32) return false;
        for (const char *p = text; *p; ++p) if (!(*p >= 'a' && *p <= 'z') && !isdigit((unsigned char)*p) && *p != '_' && *p != '-') return false;
        return true;
    }
    if (strcmp(pattern, "^[a-f0-9]{64}$") == 0) {
        if (n != 64) return false;
        for (const char *p = text; *p; ++p) if (!isdigit((unsigned char)*p) && !(*p >= 'a' && *p <= 'f')) return false;
        return true;
    }
    if (strstr(pattern, "/media/cover/")) {
        const char *prefix = "/api/v1/media/cover/"; size_t start = strlen(prefix);
        if (n <= start + 4 || strncmp(text, prefix, start) || strcmp(text + n - 4, ".jpg")) return false;
        for (size_t i = start; i < n - 4; ++i) if (!isdigit((unsigned char)text[i]) && !(text[i] >= 'a' && text[i] <= 'f')) return false;
        return true;
    }
    const char *path = text;
    if (pattern[1] == 'h') {
        if (strncmp(text, "http://", 7) == 0) path = strchr(text + 7, '/');
        else if (strncmp(text, "https://", 8) == 0) path = strchr(text + 8, '/');
        else return false;
        if (!path) return false;
    }
    size_t length = strlen(path); const char *prefix = "/api/v1/ota/";
    return length > strlen(prefix) + 4 && !strncmp(path, prefix, strlen(prefix)) &&
        !strcmp(path + length - 4, ".bin") && !strchr(path + strlen(prefix), '/');
}
#include "proto_checks.inc"
bool proto_json_text_valid(const char *text, size_t length)
{
    bool string = false;
    for (size_t i = 0; i < length; ++i) {
        if (text[i] == 0) return false;
        if (text[i] == '"') string = !string;
        else if (string && text[i] == '\\' && i + 1 < length) {
            if (i + 5 < length && text[i + 1] == 'u' && !memcmp(text + i + 2, "0000", 4)) return false;
            ++i;
        }
    }
    return true;
}
bool proto_parse(const char *text, size_t length, proto_frame_t *out)
{
    *out = (proto_frame_t){.topic = VM_NONE};
    if (!length || length > 8192 || !proto_json_text_valid(text, length)) return false;
    const char *end;
    out->root = cJSON_ParseWithLengthOpts(text, length, &end, false);
    if (!cJSON_IsObject(out->root)) goto invalid;
    while (end < text + length && isspace((unsigned char)*end)) ++end;
    if (end != text + length) goto invalid;
    const cJSON *version = cJSON_GetObjectItemCaseSensitive(out->root, "v");
    const cJSON *type = cJSON_GetObjectItemCaseSensitive(out->root, "t");
    const cJSON *ts = cJSON_GetObjectItemCaseSensitive(out->root, "ts");
    const cJSON *id = cJSON_GetObjectItemCaseSensitive(out->root, "id");
    out->data = cJSON_GetObjectItemCaseSensitive(out->root, "d");
    if (!integer(version) || version->valueint != 1 || !cJSON_IsString(type) ||
        !integer(ts) || ts->valuedouble < 0 || (id && !integer(id)) ||
        !cJSON_IsObject(out->data) || !validate_data(type->valuestring, out->data, out->root)) goto invalid;
    out->type = type->valuestring;
    if (!strncmp(out->type, "vm.", 3)) out->topic = vm_topic_from_name(out->type + 3);
    return true;
invalid:
    proto_frame_free(out); return false;
}
void proto_frame_free(proto_frame_t *frame)
{
    cJSON_Delete(frame->root); *frame = (proto_frame_t){.topic = VM_NONE};
}
