// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <stdint.h>
#include <stddef.h>
#include "esp_err.h"
esp_err_t i2c_bus_init(void);
esp_err_t i2c_bus_rd(uint8_t addr, uint8_t reg, uint8_t *buf, size_t n);
esp_err_t i2c_bus_wr(uint8_t addr, uint8_t reg, const uint8_t *buf, size_t n);
esp_err_t i2c_bus_rd16(uint8_t addr, uint16_t reg, uint8_t *buf, size_t n);
