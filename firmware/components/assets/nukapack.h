// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
#include "lvgl.h"
#define NUKAPACK_ABI 1
#define NUKAPACK_BANK_SIZE (3u * 1024u * 1024u)

typedef struct {
    char magic[4];
    uint16_t abi, count;
    uint32_t index_offset, size;
    uint8_t body_sha256[32];
} nukapack_header_t;

esp_err_t nukapack_init(void);
esp_err_t nukapack_check_bank(unsigned slot, size_t expected_size, const char *expected_version);
const char *nuka_assets_version(void);
const void *nukapack_find(const char *name, size_t *size);
const lv_font_t *nuka_font(unsigned size, bool chinese);
const lv_font_t *nuka_mono_font(void);
