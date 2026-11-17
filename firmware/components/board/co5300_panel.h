// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <stdint.h>
#include <stddef.h>
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
esp_err_t co5300_panel_init(esp_lcd_panel_handle_t *panel, esp_lcd_panel_io_handle_t *io);
esp_err_t co5300_command(uint8_t command, const void *data, size_t size);
