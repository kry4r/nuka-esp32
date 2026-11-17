// SPDX-License-Identifier: AGPL-3.0-or-later
#include "cst9220_touch.h"
#include "pins.h"
#include "i2c_bus.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_check.h"
esp_err_t touch_init(void)
{
    gpio_config_t cfg = {.pin_bit_mask = 1ULL << PIN_TOUCH_RST, .mode = GPIO_MODE_OUTPUT};
    ESP_ERROR_CHECK(gpio_config(&cfg));
    gpio_set_level(PIN_TOUCH_RST, 0); vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(PIN_TOUCH_RST, 1); vTaskDelay(pdMS_TO_TICKS(50));
    cfg = (gpio_config_t){.pin_bit_mask = 1ULL << PIN_TOUCH_INT, .mode = GPIO_MODE_INPUT,
        .pull_up_en = true, .intr_type = GPIO_INTR_NEGEDGE};
    return gpio_config(&cfg);
}
esp_err_t touch_read(touch_sample_t *out)
{
    uint8_t data[10];
    ESP_RETURN_ON_ERROR(i2c_bus_rd16(0x5a, 0xd000, data, sizeof(data)), "touch", "read");
    out->pressed = (data[5] & 0x7f) != 0 && (data[0] & 15) == 6;
    uint16_t sensor_x = ((uint16_t)data[1] << 4) | (data[3] >> 4);
    uint16_t sensor_y = ((uint16_t)data[2] << 4) | (data[3] & 15);
    // The mounted sensor swaps axes and mirrors sensor Y relative to the panel.
    out->x = 479 - sensor_y;
    out->y = sensor_x;
    return ESP_OK;
}
