// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <stdbool.h>
#include "esp_err.h"
typedef struct { float x, y, z; } imu_sample_t;
esp_err_t imu_init(void);
esp_err_t imu_set_sleep(bool asleep);
esp_err_t imu_read(imu_sample_t *sample);
bool imu_motion_pending(void);
