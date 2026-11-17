// SPDX-License-Identifier: AGPL-3.0-or-later
#include "co5300_panel.h"
#include "pins.h"
#include "driver/spi_master.h"
#include "esp_lcd_co5300.h"
static esp_lcd_panel_io_handle_t panel_io;
#define CMD(c, n, delay, ...) {c, (const uint8_t[]){__VA_ARGS__}, n, delay}
// The sequence is the hardware fact specified in SPEC 6.3.3.
static const co5300_lcd_init_cmd_t init_commands[] = {
    CMD(0x11,0,600,0), CMD(0xFE,1,0,0x20), CMD(0x19,1,0,0x10),
    CMD(0x1C,1,0,0xA0), CMD(0xFE,1,0,0x00), CMD(0xC4,1,0,0x80),
    CMD(0x3A,1,0,0x55), CMD(0x35,1,0,0x00), CMD(0x53,1,0,0x20),
    CMD(0x51,1,0,0xFF), CMD(0x63,1,0,0xFF),
    CMD(0x2A,4,0,0,0,0x01,0xDF), CMD(0x2B,4,0,0,0,0x01,0xDF),
    CMD(0x36,1,0,0xA0), CMD(0x29,0,600,0)
};
esp_err_t co5300_panel_init(esp_lcd_panel_handle_t *panel, esp_lcd_panel_io_handle_t *io)
{
    spi_bus_config_t bus = CO5300_PANEL_BUS_QSPI_CONFIG(
        PIN_LCD_CLK, PIN_LCD_D0, PIN_LCD_D1, PIN_LCD_D2, PIN_LCD_D3, 480 * 48 * 2);
    ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_CH_AUTO));
    esp_lcd_panel_io_spi_config_t cfg = CO5300_PANEL_IO_QSPI_CONFIG(PIN_LCD_CS, NULL, NULL);
    cfg.pclk_hz = 40000000;
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)SPI2_HOST, &cfg, &panel_io));
    const co5300_vendor_config_t vendor = {
        .init_cmds = init_commands, .init_cmds_size = sizeof(init_commands) / sizeof(init_commands[0]),
        .flags.use_qspi_interface = true
    };
    const esp_lcd_panel_dev_config_t device = {
        .reset_gpio_num = PIN_LCD_RST, .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = 16, .vendor_config = (void *)&vendor
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_co5300(panel_io, &device, panel));
    ESP_ERROR_CHECK(esp_lcd_panel_reset(*panel));
    ESP_ERROR_CHECK(esp_lcd_panel_init(*panel));
    ESP_ERROR_CHECK(esp_lcd_panel_set_gap(*panel, 0, 0));
    uint8_t zero = 0; ESP_ERROR_CHECK(co5300_command(0x51, &zero, 1));
    *io = panel_io;
    return ESP_OK;
}
esp_err_t co5300_command(uint8_t command, const void *data, size_t size)
{
    return esp_lcd_panel_io_tx_param(panel_io, (0x02U << 24) | ((uint32_t)command << 8), data, size);
}
