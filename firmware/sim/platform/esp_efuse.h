// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
typedef int esp_efuse_block_t;
#define EFUSE_BLK_KEY0 4
#define ESP_EFUSE_KEY_PURPOSE_HMAC_UP 8
static inline int esp_efuse_get_key_purpose(esp_efuse_block_t block) { (void)block; return 0; }
