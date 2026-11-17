// SPDX-License-Identifier: AGPL-3.0-or-later
#include "qmi8658.h"
#include "i2c_bus.h"
#include "pins.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
static esp_err_t write8(uint8_t reg, uint8_t value) { return i2c_bus_wr(0x6B, reg, &value, 1); }
static esp_err_t command(uint8_t cmd)
{
    esp_err_t err = write8(0x0A, cmd);
    if (err != ESP_OK) return err;
    int64_t deadline = esp_timer_get_time() + 1000000;
    uint8_t status;
    do {
        err = i2c_bus_rd(0x6B, 0x2D, &status, 1);
        if (err != ESP_OK) return err;
        if (esp_timer_get_time() >= deadline) return ESP_ERR_TIMEOUT;
        vTaskDelay(pdMS_TO_TICKS(1));
    } while (!(status & 0x80));
    err = write8(0x0A, 0);
    if (err != ESP_OK) return err;
    do {
        err = i2c_bus_rd(0x6B, 0x2D, &status, 1);
        if (err != ESP_OK) return err;
        if (esp_timer_get_time() >= deadline) return ESP_ERR_TIMEOUT;
        vTaskDelay(pdMS_TO_TICKS(1));
    } while (status & 0x80);
    return ESP_OK;
}
esp_err_t imu_init(void)
{
    gpio_config_t pin = {.pin_bit_mask = 1ULL << PIN_IMU_INT1, .mode = GPIO_MODE_INPUT};
    ESP_ERROR_CHECK(gpio_config(&pin));
    ESP_ERROR_CHECK(write8(0x60, 0xB0)); vTaskDelay(pdMS_TO_TICKS(20));
    uint8_t id; ESP_ERROR_CHECK(i2c_bus_rd(0x6B, 0, &id, 1));
    if (id != 0x05) return ESP_ERR_INVALID_RESPONSE;
    ESP_ERROR_CHECK(write8(0x09, 0x80));
    return imu_set_sleep(false);
}
esp_err_t imu_set_sleep(bool asleep)
{
    esp_err_t err = write8(0x08, 0);
    if (err != ESP_OK) return err;
    err = write8(0x02, asleep ? 0x48 : 0x40);
    if (err != ESP_OK) return err;
    err = write8(0x03, asleep ? 0x2D : 0x28);
    if (err != ESP_OK) return err;
    if (asleep) {
        err = write8(0x0B, 200);
        if (err != ESP_OK) return err;
        err = write8(0x0C, 0x20);
        if (err != ESP_OK) return err;
        err = command(0x08);
        if (err != ESP_OK) return err;
    }
    return write8(0x08, 1);
}
esp_err_t imu_read(imu_sample_t *sample)
{
    uint8_t bytes[6]; esp_err_t err = i2c_bus_rd(0x6B, 0x35, bytes, sizeof(bytes));
    if (err != ESP_OK) return err;
    *sample = (imu_sample_t){
        (int16_t)(bytes[0] | bytes[1] << 8) / 4096.0f,
        (int16_t)(bytes[2] | bytes[3] << 8) / 4096.0f,
        (int16_t)(bytes[4] | bytes[5] << 8) / 4096.0f};
    return ESP_OK;
}
bool imu_motion_pending(void) { return gpio_get_level(PIN_IMU_INT1) != 0; }
