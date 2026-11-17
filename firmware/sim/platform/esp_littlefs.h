// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "esp_err.h"
#include <stddef.h>
esp_err_t esp_littlefs_info(const char *label, size_t *total, size_t *used);
