// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "cJSON.h"
#include "esp_err.h"
void diag_start(void);
cJSON *diag_snapshot(void);
void diag_stream(bool enabled);
esp_err_t diag_dump(uint8_t **data, size_t *length);
