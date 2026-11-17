// SPDX-License-Identifier: AGPL-3.0-or-later
#include "nukapack.h"
#include "store.h"
#include <string.h>
#include "esp_partition.h"
#include "mbedtls/sha256.h"

typedef struct { uint32_t hash, offset, size, type; } entry_t;
typedef struct {
    char magic[4]; uint16_t size, line_height;
    int16_t base_line, underline_position;
    int8_t underline_thickness; uint8_t bpp;
    uint16_t count;
    uint32_t glyph_offset, unicode_offset, bitmap_offset, bitmap_size;
} font_header_t;
typedef struct {
    lv_font_t font;
    lv_font_fmt_txt_dsc_t descriptor;
    lv_font_fmt_txt_cmap_t cmap;
} mapped_font_t;

_Static_assert(sizeof(nukapack_header_t) == 48, "nukapack ABI header");
_Static_assert(sizeof(font_header_t) == 32, "font ABI header");
_Static_assert(sizeof(lv_font_fmt_txt_glyph_dsc_t) == 16,
               "Enable LV_FONT_FMT_TXT_LARGE for Chinese font bitmaps");
static const uint8_t *mapped;
static esp_partition_mmap_handle_t mmap_handle;
static mapped_font_t fonts[9];
static bool has_chinese;
static const char *pack_version = "";
static const char *font_names[] = {
    "inter-22", "inter-28", "inter-32", "inter-40", "inter-56", "clock-112", "mono-40", "zh-22", "zh-28"
};

static uint32_t name_hash(const char *name)
{
    uint32_t hash = 2166136261u;
    for (const uint8_t *byte = (const uint8_t *)name; *byte; ++byte)
        hash = (hash ^ *byte) * 16777619u;
    return hash;
}

static const void *find_in(const uint8_t *base, const char *name, size_t *size)
{
    const nukapack_header_t *header = (const nukapack_header_t *)base;
    const entry_t *entries = (const entry_t *)(base + header->index_offset);
    uint32_t hash = name_hash(name);
    for (unsigned i = 0; i < header->count; ++i) {
        if (entries[i].hash == hash) {
            *size = entries[i].size;
            return base + entries[i].offset;
        }
    }
    return NULL;
}
const void *nukapack_find(const char *name, size_t *size)
{ return find_in(mapped, name, size); }

static esp_err_t bind_font(mapped_font_t *out, const font_header_t *header, size_t size)
{
    if (size < sizeof(*header) || memcmp(header->magic, "NFN1", 4) || header->bpp != 4 ||
        header->glyph_offset != sizeof(*header) ||
        header->unicode_offset != header->glyph_offset + (header->count + 1u) * 16u ||
        header->unicode_offset + header->count * 2u > size ||
        header->bitmap_offset > size || header->bitmap_size > size - header->bitmap_offset)
        return ESP_ERR_INVALID_SIZE;
    const uint8_t *base = (const uint8_t *)header;
    out->cmap = (lv_font_fmt_txt_cmap_t){
        .range_start = 0, .range_length = UINT16_MAX, .glyph_id_start = 1,
        .unicode_list = (const uint16_t *)(base + header->unicode_offset),
        .list_length = header->count, .type = LV_FONT_FMT_TXT_CMAP_SPARSE_TINY
    };
    out->descriptor = (lv_font_fmt_txt_dsc_t){
        .glyph_bitmap = base + header->bitmap_offset,
        .glyph_dsc = (const lv_font_fmt_txt_glyph_dsc_t *)(base + header->glyph_offset),
        .cmaps = &out->cmap, .cmap_num = 1, .bpp = 4,
        .bitmap_format = LV_FONT_FMT_TXT_PLAIN
    };
    out->font = (lv_font_t){
        .dsc = &out->descriptor, .get_glyph_dsc = lv_font_get_glyph_dsc_fmt_txt,
        .get_glyph_bitmap = lv_font_get_bitmap_fmt_txt,
        .line_height = header->line_height, .base_line = header->base_line,
        .underline_position = header->underline_position,
        .underline_thickness = header->underline_thickness,
        .static_bitmap = 1
    };
    return ESP_OK;
}

static esp_err_t load_bank(unsigned slot, mapped_font_t *font_set, bool *chinese,
    const uint8_t **base, esp_partition_mmap_handle_t *handle)
{
    const esp_partition_t *partition = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, 0x40, "assets");
    size_t bank = slot * NUKAPACK_BANK_SIZE;
    nukapack_header_t header;
    esp_err_t error = esp_partition_read(partition, bank, &header, sizeof(header));
    if (error != ESP_OK) return error;
    if (memcmp(header.magic, "NUKA", 4) || header.abi != NUKAPACK_ABI)
        return ESP_ERR_INVALID_VERSION;
    if (header.size > NUKAPACK_BANK_SIZE || header.index_offset != sizeof(header) ||
        header.size < sizeof(header) + header.count * sizeof(entry_t))
        return ESP_ERR_INVALID_SIZE;
    error = esp_partition_mmap(partition, bank, header.size, ESP_PARTITION_MMAP_DATA,
                              (const void **)base, handle);
    if (error != ESP_OK) return error;
    uint8_t digest[32];
    int result = mbedtls_sha256(*base + sizeof(header), header.size - sizeof(header), digest, 0);
    if (result || memcmp(digest, header.body_sha256, sizeof(digest))) {
        error = ESP_ERR_INVALID_CRC; goto invalid;
    }
    const entry_t *entries = (const entry_t *)(*base + header.index_offset);
    for (unsigned i = 0; i < header.count; ++i) {
        const entry_t *entry = &entries[i];
        if ((entry->offset & 3u) || entry->offset < sizeof(header) + header.count * sizeof(entry_t) ||
            entry->offset > header.size || entry->size > header.size - entry->offset)
            { error = ESP_ERR_INVALID_SIZE; goto invalid; }
    }
    size_t size;
    const char *version = find_in(*base, "version", &size);
    if (!version || size < 2 || version[size - 1]) { error = ESP_ERR_INVALID_VERSION; goto invalid; }
    for (size_t i = 0; i < size - 1; ++i) {
        if ((uint8_t)version[i] < 32 || (uint8_t)version[i] > 126) {
            error = ESP_ERR_INVALID_VERSION; goto invalid;
        }
    }
    *chinese = find_in(*base, "zh-22", &size) != NULL;
    for (unsigned i = 0; i < (*chinese ? 9u : 7u); ++i) {
        const font_header_t *font = find_in(*base, font_names[i], &size);
        if (!font) { error = ESP_ERR_NOT_FOUND; goto invalid; }
        error = bind_font(&font_set[i], font, size);
        if (error != ESP_OK) goto invalid;
    }
    return ESP_OK;
invalid:
    esp_partition_munmap(*handle); return error;
}
esp_err_t nukapack_init(void)
{
    esp_err_t error = load_bank(nuka_settings.asset_slot, fonts, &has_chinese, &mapped, &mmap_handle);
    if (error == ESP_OK) { size_t size; pack_version = find_in(mapped, "version", &size); }
    return error;
}
esp_err_t nukapack_check_bank(unsigned slot, size_t expected_size, const char *expected_version)
{
    mapped_font_t temporary[9]; bool chinese;
    const uint8_t *base; esp_partition_mmap_handle_t handle;
    esp_err_t error = load_bank(slot, temporary, &chinese, &base, &handle);
    if (error != ESP_OK) return error;
    error = ((const nukapack_header_t *)base)->size == expected_size ? ESP_OK : ESP_ERR_INVALID_SIZE;
    size_t size;
    if (error == ESP_OK && strcmp(find_in(base, "version", &size), expected_version))
        error = ESP_ERR_INVALID_VERSION;
    esp_partition_munmap(handle); return error;
}
const char *nuka_assets_version(void) { return pack_version; }

const lv_font_t *nuka_font(unsigned size, bool chinese)
{
    unsigned index = size == 22 ? 0 : size == 28 ? 1 : size == 32 ? 2 :
                     size == 40 ? 3 : size == 56 ? 4 : 5;
    if (chinese && has_chinese && size != 112) {
        unsigned zh = size == 22 ? 7 : 8;
        fonts[index].font.fallback = &fonts[zh].font;
    } else {
        fonts[index].font.fallback = NULL;
    }
    return &fonts[index].font;
}

const lv_font_t *nuka_mono_font(void) { return &fonts[6].font; }
