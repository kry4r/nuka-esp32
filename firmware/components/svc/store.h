// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include "esp_err.h"
#include "cJSON.h"
typedef struct {
    char lang[6]; uint8_t brightness; uint32_t t_dim, t_off;
    bool raise, clock_style, dnd, always_on;
    uint8_t flip_action, charge_ma_index, charge_mode;
    char key3_action[16], key3_double_card[16]; uint8_t key3_macro;
    uint16_t local_overrides; uint8_t asset_slot;
    char cards[192]; uint32_t boot_count;
    char home_face[10]; uint8_t volume; bool assistant;
} nuka_settings_t;
extern nuka_settings_t nuka_settings;
esp_err_t store_init(void);
esp_err_t store_save_settings(void);
esp_err_t store_read_json(const char *ns, const char *key, cJSON **out);
esp_err_t store_write_json(const char *ns, const char *key, const cJSON *data);
esp_err_t store_read_string(const char *ns, const char *key, char *out, size_t size);
esp_err_t store_write_string(const char *ns, const char *key, const char *value);
esp_err_t store_write_u32(const char *ns, const char *key, uint32_t value);
esp_err_t store_read_u32(const char *ns, const char *key, uint32_t *out);
esp_err_t store_factory_reset(bool erase_vault);
