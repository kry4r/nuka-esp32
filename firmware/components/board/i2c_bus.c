// SPDX-License-Identifier: AGPL-3.0-or-later
#include "i2c_bus.h"
#include <string.h>
#include <stdio.h>
#include "driver/i2c_master.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "pins.h"
static i2c_master_bus_handle_t bus;
static i2c_master_dev_handle_t devices[128];
static SemaphoreHandle_t mutex;
esp_err_t i2c_bus_init(void)
{
    mutex = xSemaphoreCreateMutex();
    ESP_ERROR_CHECK(gpio_hold_dis(PIN_I2C_SDA));
    ESP_ERROR_CHECK(gpio_hold_dis(PIN_I2C_SCL));
    gpio_config_t pins = {.pin_bit_mask = (1ULL << PIN_I2C_SDA) | (1ULL << PIN_I2C_SCL),
        .mode = GPIO_MODE_INPUT_OUTPUT_OD, .pull_up_en = true};
    ESP_ERROR_CHECK(gpio_config(&pins));
    ESP_ERROR_CHECK(gpio_set_level(PIN_I2C_SDA, 1));
    ESP_ERROR_CHECK(gpio_set_level(PIN_I2C_SCL, 1));
    ESP_LOGI("i2c", "released scl=%d sda=%d", gpio_get_level(PIN_I2C_SCL), gpio_get_level(PIN_I2C_SDA));
    i2c_master_bus_config_t cfg = {
        .i2c_port = I2C_NUM_0, .sda_io_num = PIN_I2C_SDA, .scl_io_num = PIN_I2C_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT, .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true
    };
    ESP_ERROR_CHECK(i2c_new_master_bus(&cfg, &bus));
    gpio_dump_io_configuration(stdout, (1ULL << PIN_I2C_SDA) | (1ULL << PIN_I2C_SCL));
    ESP_LOGI("i2c", "lines scl=%d sda=%d", gpio_get_level(PIN_I2C_SCL), gpio_get_level(PIN_I2C_SDA));
    const uint8_t onboard[] = {0x34, 0x5A, 0x6B, 0x51};
    for (unsigned i = 0; i < sizeof(onboard); ++i)
        ESP_LOGI("i2c", "device=0x%02x probe=%s", onboard[i], esp_err_to_name(i2c_master_probe(bus, onboard[i], 100)));
    const uint8_t addresses[] = {0x34, 0x5a, 0x6b, 0x51, 0x18, 0x40};
    for (size_t i = 0; i < sizeof(addresses); ++i) {
        i2c_device_config_t device = {.dev_addr_length = I2C_ADDR_BIT_LEN_7,
            .device_address = addresses[i], .scl_speed_hz = 400000};
        ESP_ERROR_CHECK(i2c_master_bus_add_device(bus, &device, &devices[addresses[i]]));
    }
    return ESP_OK;
}
esp_err_t i2c_bus_rd(uint8_t addr, uint8_t reg, uint8_t *buf, size_t n)
{
    xSemaphoreTake(mutex, portMAX_DELAY);
    esp_err_t err = i2c_master_transmit_receive(devices[addr], &reg, 1, buf, n, 100);
    xSemaphoreGive(mutex); return err;
}
esp_err_t i2c_bus_wr(uint8_t addr, uint8_t reg, const uint8_t *buf, size_t n)
{
    uint8_t bytes[n + 1]; bytes[0] = reg; memcpy(bytes + 1, buf, n);
    xSemaphoreTake(mutex, portMAX_DELAY);
    esp_err_t err = i2c_master_transmit(devices[addr], bytes, n + 1, 100);
    xSemaphoreGive(mutex); return err;
}
esp_err_t i2c_bus_rd16(uint8_t addr, uint16_t reg, uint8_t *buf, size_t n)
{
    uint8_t address[] = {reg >> 8, reg & 0xff};
    xSemaphoreTake(mutex, portMAX_DELAY);
    esp_err_t err = i2c_master_transmit(devices[addr], address, 2, 100);
    if (err == ESP_OK) {
        vTaskDelay(pdMS_TO_TICKS(2));
        err = i2c_master_receive(devices[addr], buf, n, 100);
    }
    xSemaphoreGive(mutex); return err;
}
