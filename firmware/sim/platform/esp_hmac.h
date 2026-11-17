// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "esp_err.h"
#include <stddef.h>
#include <stdint.h>
typedef unsigned hmac_key_id_t;
static inline esp_err_t esp_hmac_calculate(hmac_key_id_t slot, const void *data, size_t size, uint8_t *output)
{ (void)slot; (void)data; (void)size; (void)output; return ESP_ERR_NOT_SUPPORTED; }
