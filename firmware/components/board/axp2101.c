// SPDX-License-Identifier: AGPL-3.0-or-later
#include "axp2101.h"
#include "i2c_bus.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_check.h"
static axp_boot_reason_t boot_reason;
static esp_err_t write_reg(uint8_t reg, uint8_t value) { return i2c_bus_wr(0x34, reg, &value, 1); }
static esp_err_t update_reg(uint8_t reg, uint8_t mask, uint8_t value)
{
    uint8_t old;
    ESP_RETURN_ON_ERROR(i2c_bus_rd(0x34, reg, &old, 1), "axp", "read");
    return write_reg(reg, (old & ~mask) | value);
}
esp_err_t axp_set_charge(uint16_t ma, bool full_charge)
{
    uint8_t current_code = ma == 100 ? 4 : 6 + ma / 100;
    ESP_RETURN_ON_ERROR(update_reg(0x62, 0x1f, current_code), "axp", "charge current");
    return update_reg(0x64, 7, full_charge ? 3 : 2);
}
esp_err_t axp_init(const axp_cfg_t *cfg)
{
    ESP_ERROR_CHECK(i2c_bus_rd(0x34, 0x20, &boot_reason.on, 1));
    ESP_ERROR_CHECK(i2c_bus_rd(0x34, 0x21, &boot_reason.off, 1));
    ESP_ERROR_CHECK(write_reg(0x82, 18));
    ESP_ERROR_CHECK(update_reg(0x80, 0x1f, 1));
    ESP_ERROR_CHECK(write_reg(0x92, 28));
    ESP_ERROR_CHECK(write_reg(0x90, 0));
    ESP_ERROR_CHECK(write_reg(0x91, 0));
    ESP_ERROR_CHECK(update_reg(0x50, 0x1f, 0x10));
    ESP_ERROR_CHECK(write_reg(0x30, 0x1d));
    ESP_ERROR_CHECK(update_reg(0x68, 1, 1));
    ESP_ERROR_CHECK(update_reg(0xa2, 1, 1));
    ESP_ERROR_CHECK(update_reg(0x18, 2, 2));
    ESP_ERROR_CHECK(write_reg(0x61, 2));
    ESP_ERROR_CHECK(write_reg(0x63, 0x11));
    ESP_ERROR_CHECK(axp_set_charge(cfg->charge_ma, cfg->full_charge));
    ESP_ERROR_CHECK(update_reg(0x16, 7, 4));
    ESP_ERROR_CHECK(update_reg(0x27, 0x3f, 0x16));
    ESP_ERROR_CHECK(update_reg(0x22, 7, 6));
    return ESP_OK;
}
esp_err_t axp_read_status(axp_status_t *out)
{
    uint8_t status[2], adc[10], pct;
    ESP_RETURN_ON_ERROR(i2c_bus_rd(0x34, 0, status, sizeof(status)), "axp", "status");
    ESP_RETURN_ON_ERROR(i2c_bus_rd(0x34, 0x34, adc, sizeof(adc)), "axp", "adc");
    ESP_RETURN_ON_ERROR(i2c_bus_rd(0x34, 0xa4, &pct, 1), "axp", "percent");
    *out = (axp_status_t){
        .vbat_mv = ((adc[0] & 0x1f) << 8) | adc[1],
        .vbus_mv = ((adc[4] & 0x3f) << 8) | adc[5], .pct = pct & 0x7f,
        .vbus_present = !!(status[0] & 0x20), .batt_present = !!(status[0] & 8),
        .charging = (status[1] & 0x60) == 0x20,
        .full = (status[1] & 7) == 4,
        .die_temp = 22.0f + (7274 - (((adc[8] & 0x3f) << 8) | adc[9])) / 20.0f
    };
    return ESP_OK;
}
uint32_t axp_take_irq(void)
{
    uint8_t flags[3]; ESP_ERROR_CHECK(i2c_bus_rd(0x34, 0x48, flags, 3));
    ESP_ERROR_CHECK(i2c_bus_wr(0x34, 0x48, flags, 3));
    return flags[0] | ((uint32_t)flags[1] << 8) | ((uint32_t)flags[2] << 16);
}
esp_err_t axp_set_aldo1(bool on) { return update_reg(0x90, 1, on ? 1 : 0); }
axp_boot_reason_t axp_boot_reason(void) { return boot_reason; }
void axp_power_off(void)
{
    ESP_ERROR_CHECK(update_reg(0x10, 1, 1));
    for (;;) vTaskDelay(portMAX_DELAY);
}
