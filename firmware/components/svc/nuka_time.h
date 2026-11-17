// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <time.h>
#include <stdbool.h>
#include "esp_err.h"
esp_err_t nuka_time_init(void);
esp_err_t nuka_time_update(time_t epoch, const char *tz_posix);
esp_err_t nuka_time_sntp(const char *const *servers, unsigned count);
esp_err_t nuka_time_poll(void);
bool nuka_time_stale(void);
