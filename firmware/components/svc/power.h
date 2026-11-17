// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <stdbool.h>
#include <stdint.h>
typedef enum { POWER_STARTING, POWER_ACTIVE, POWER_DIM, POWER_SCREEN_OFF, POWER_SHUTDOWN } power_state_t;
void power_start(void);
void power_usb_ready(void);
void power_init(void);
void power_reload_settings(void);
void power_activity(void);
void power_screen_off(void);
bool power_screen_on(void);
uint32_t power_screen_off_seconds(void);
void power_shutdown(const char *reason);
void power_set_permission_pending(bool pending);
void power_set_battery_limits(unsigned pct, bool charging, bool usb);
