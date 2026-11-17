// SPDX-License-Identifier: AGPL-3.0-or-later
#include "pcf85063.h"
#include "i2c_bus.h"
#include "esp_check.h"
static int decode(uint8_t b) { return (b >> 4) * 10 + (b & 15); }
static uint8_t encode(int n) { return (n / 10 << 4) | n % 10; }
esp_err_t rtc_get(struct tm *t)
{
    uint8_t b[7]; ESP_RETURN_ON_ERROR(i2c_bus_rd(0x51, 4, b, 7), "rtc", "read");
    if (b[0] & 0x80) return ESP_ERR_INVALID_STATE;
    *t = (struct tm){.tm_sec = decode(b[0] & 0x7f), .tm_min = decode(b[1] & 0x7f),
        .tm_hour = decode(b[2] & 0x3f), .tm_mday = decode(b[3] & 0x3f),
        .tm_wday = b[4] & 7, .tm_mon = decode(b[5] & 0x1f) - 1,
        .tm_year = 100 + decode(b[6]), .tm_isdst = 0};
    return ESP_OK;
}
esp_err_t rtc_set(const struct tm *t)
{
    uint8_t b[] = {encode(t->tm_sec), encode(t->tm_min), encode(t->tm_hour),
        encode(t->tm_mday), t->tm_wday, encode(t->tm_mon + 1), encode(t->tm_year - 100)};
    return i2c_bus_wr(0x51, 4, b, sizeof(b));
}
