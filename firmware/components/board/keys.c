// SPDX-License-Identifier: AGPL-3.0-or-later
#include "keys.h"
#include "pins.h"
#include "driver/gpio.h"
static const int pins[] = {PIN_PWR, PIN_BOOT, PIN_KEY3};
esp_err_t keys_init(void)
{
    gpio_config_t cfg = {.pin_bit_mask = (1ULL << PIN_BOOT) | (1ULL << PIN_KEY3),
        .mode = GPIO_MODE_INPUT, .pull_up_en = true};
    ESP_ERROR_CHECK(gpio_config(&cfg));
    cfg = (gpio_config_t){.pin_bit_mask = 1ULL << PIN_PWR, .mode = GPIO_MODE_INPUT};
    return gpio_config(&cfg);
}
bool keys_pressed(key_id_t key) { return gpio_get_level(pins[key]) == (key == KEY_PWR); }
