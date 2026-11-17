// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
esp_err_t gfx_init(void);
void gfx_start(void (*bootstrap)(void), void (*tick)(void));
esp_err_t gfx_set_brightness(uint8_t pct);
esp_err_t gfx_fade_to(uint8_t pct, uint32_t ms);
esp_err_t gfx_suspend(void);
esp_err_t gfx_resume(void);
uint32_t gfx_flush_count(void);
uint32_t gfx_frame_count(void);
void gfx_set_touch_observer(void (*observer)(bool pressed,int x,int y));
