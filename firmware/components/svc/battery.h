// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <stdbool.h>
#include "axp2101.h"
#include "cJSON.h"
void battery_start(void);
axp_status_t battery_status(void);
cJSON *battery_json(void);
bool battery_low_power(void);
void battery_estimates(float *screen_hours, float *standby_hours);
