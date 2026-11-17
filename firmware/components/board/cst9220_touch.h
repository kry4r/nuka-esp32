// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
typedef struct { bool pressed; uint16_t x, y; } touch_sample_t;
esp_err_t touch_init(void);
esp_err_t touch_read(touch_sample_t *out);
