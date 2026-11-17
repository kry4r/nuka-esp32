// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "app.h"
#include "imu_gesture.h"
#include "esp_err.h"
typedef struct {
    const char *phase;
    bool running;
    unsigned remaining, round, minutes[3];
} pomodoro_status_t;
esp_err_t pomodoro_init(void);
esp_err_t pomodoro_tick(void);
void pomodoro_gesture(gesture_t gesture);
pomodoro_status_t pomodoro_status(void);
esp_err_t pomodoro_set_minutes(unsigned focus, unsigned short_break, unsigned long_break);
