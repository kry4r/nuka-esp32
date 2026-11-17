// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
typedef struct { uint16_t charge_ma; bool full_charge; } axp_cfg_t;
typedef struct {
    uint16_t vbat_mv, vbus_mv; uint8_t pct;
    bool charging, full, vbus_present, batt_present; float die_temp;
} axp_status_t;
typedef struct { uint8_t on, off; } axp_boot_reason_t;
esp_err_t axp_init(const axp_cfg_t *cfg);
esp_err_t axp_read_status(axp_status_t *out);
esp_err_t axp_set_charge(uint16_t ma, bool full_charge);
esp_err_t axp_set_aldo1(bool on);
uint32_t axp_take_irq(void);
axp_boot_reason_t axp_boot_reason(void);
void axp_power_off(void) __attribute__((noreturn));
