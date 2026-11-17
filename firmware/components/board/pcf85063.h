// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <time.h>
#include "esp_err.h"
esp_err_t rtc_get(struct tm *t);
esp_err_t rtc_set(const struct tm *t);
