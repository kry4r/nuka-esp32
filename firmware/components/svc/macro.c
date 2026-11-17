// SPDX-License-Identifier: AGPL-3.0-or-later
#include "macro.h"
#include "store.h"
#include "usb.h"
#include "nvs.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static uint8_t key_code(const char *key)
{
    if (key[0] >= 'A' && key[0] <= 'Z' && !key[1]) return key[0] - 'A' + 4;
    if (key[0] >= '1' && key[0] <= '9' && !key[1]) return key[0] - '1' + 30;
    if (key[0] == '0' && !key[1]) return 39;
    const struct { const char *name; uint8_t code; } names[] = {
        {"ENTER",40},{"ESC",41},{"BACKSPACE",42},{"TAB",43},{"SPACE",44},
        {"INSERT",73},{"HOME",74},{"PAGEUP",75},{"DELETE",76},{"END",77},
        {"PAGEDOWN",78},{"RIGHT",79},{"LEFT",80},{"DOWN",81},{"UP",82}};
    for (unsigned i = 0; i < sizeof(names) / sizeof(names[0]); ++i)
        if (strcmp(key, names[i].name) == 0) return names[i].code;
    if (key[0] == 'F' && key[1]) {
        char *end; unsigned number = strtoul(key + 1, &end, 10);
        if (!*end && number >= 1 && number <= 24) return number <= 12 ? 57 + number : 103 + number - 12;
    }
    return 0;
}
static bool keys(const char *text, uint8_t *modifiers, uint8_t codes[6])
{
    char combo[128]; if (strlen(text) >= sizeof(combo)) return false;
    strcpy(combo, text); unsigned count = 0; *modifiers = 0; memset(codes, 0, 6);
    char *save;
    for (char *key = strtok_r(combo, "+", &save); key; key = strtok_r(NULL, "+", &save)) {
        if (strcmp(key, "CTRL") == 0) *modifiers |= 1;
        else if (strcmp(key, "SHIFT") == 0) *modifiers |= 2;
        else if (strcmp(key, "ALT") == 0) *modifiers |= 4;
        else if (strcmp(key, "GUI") == 0 || strcmp(key, "WIN") == 0 || strcmp(key, "CMD") == 0) *modifiers |= 8;
        else { if (count == 6 || !(codes[count] = key_code(key))) return false; ++count; }
    }
    return count != 0;
}
static bool valid_item(const cJSON *item)
{
    const cJSON *slot = cJSON_GetObjectItemCaseSensitive(item, "slot");
    const cJSON *kind = cJSON_GetObjectItemCaseSensitive(item, "kind");
    const cJSON *label = cJSON_GetObjectItemCaseSensitive(item, "label");
    const cJSON *value = cJSON_GetObjectItemCaseSensitive(item, "value");
    if (!cJSON_IsNumber(slot) || floor(slot->valuedouble) != slot->valuedouble || slot->valuedouble < 0 || slot->valuedouble > 11 ||
        !cJSON_IsString(kind) || !cJSON_IsString(label) || !cJSON_IsString(value)) return false;
    if (strcmp(kind->valuestring, "text") == 0) {
        unsigned count = 0;
        for (const unsigned char *p = (const unsigned char *)value->valuestring; *p; ++p) if ((*p & 0xC0) != 0x80) ++count;
        return count <= 256;
    }
    uint8_t modifiers, codes[6];
    return strcmp(kind->valuestring, "keys") == 0 && keys(value->valuestring, &modifiers, codes);
}
bool macro_validate(const cJSON *data)
{
    const cJSON *rev = cJSON_GetObjectItemCaseSensitive(data, "rev");
    const cJSON *items = cJSON_GetObjectItemCaseSensitive(data, "macros");
    if (!cJSON_IsNumber(rev) || !isfinite(rev->valuedouble) || rev->valuedouble < 0 || floor(rev->valuedouble) != rev->valuedouble ||
        !cJSON_IsArray(items) || cJSON_GetArraySize(items) > 12) return false;
    const cJSON *item; cJSON_ArrayForEach(item, items) if (!valid_item(item)) return false;
    return true;
}
esp_err_t macro_apply(const cJSON *data)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open("macro", NVS_READWRITE, &handle);
    if (err != ESP_OK) return err;
    err = nvs_erase_all(handle);
    const cJSON *item;
    cJSON_ArrayForEach(item, cJSON_GetObjectItemCaseSensitive(data, "macros")) {
        if (err != ESP_OK) break;
        char key[5]; snprintf(key, sizeof(key), "m%d", cJSON_GetObjectItemCaseSensitive(item, "slot")->valueint);
        char *text = cJSON_PrintUnformatted(item);
        err = nvs_set_str(handle, key, text); cJSON_free(text);
    }
    if (err == ESP_OK) err = nvs_set_u32(handle, "rev", cJSON_GetObjectItemCaseSensitive(data, "rev")->valuedouble);
    if (err == ESP_OK) err = nvs_commit(handle);
    nvs_close(handle); return err;
}
esp_err_t macro_execute(unsigned slot)
{
    char key[5]; snprintf(key, sizeof(key), "m%u", slot);
    cJSON *item; esp_err_t err = store_read_json("macro", key, &item);
    if (err == ESP_ERR_NVS_NOT_FOUND) return ESP_OK;
    if (err != ESP_OK) return err;
    if (!valid_item(item)) { cJSON_Delete(item); return ESP_ERR_INVALID_ARG; }
    const char *value = cJSON_GetObjectItemCaseSensitive(item, "value")->valuestring;
    if (strcmp(cJSON_GetObjectItemCaseSensitive(item, "kind")->valuestring, "text") == 0) usb_type_text(value);
    else {
        uint8_t modifiers, codes[6]; keys(value, &modifiers, codes);
        usb_keyboard_report(modifiers, codes); usb_keyboard_report(0, (uint8_t[6]){0});
    }
    cJSON_Delete(item); return ESP_OK;
}
unsigned macro_revision(void)
{
    uint32_t revision = 0; esp_err_t err = store_read_u32("macro", "rev", &revision);
    ESP_ERROR_CHECK(err == ESP_ERR_NVS_NOT_FOUND ? ESP_OK : err); return revision;
}
