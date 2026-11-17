// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <stdbool.h>
#include "esp_err.h"
typedef enum { KEY_PWR, KEY_BOOT, KEY_KEY3, KEY_COUNT } key_id_t;
esp_err_t keys_init(void);
bool keys_pressed(key_id_t key);
