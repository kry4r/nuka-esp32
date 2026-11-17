// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <stdbool.h>
#include "cJSON.h"
#include "esp_err.h"
bool macro_validate(const cJSON *data);
esp_err_t macro_apply(const cJSON *data);
esp_err_t macro_execute(unsigned slot);
unsigned macro_revision(void);
