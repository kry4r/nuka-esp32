// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "lvgl.h"
#include <stdbool.h>

typedef enum {
    ICON_WIFI, ICON_OFFLINE, ICON_BOLT, ICON_MOON, ICON_PLAY, ICON_PAUSE, ICON_NEXT, ICON_PREV,
    ICON_CHEVRON, ICON_BACK, ICON_LOCK, ICON_UP, ICON_DOWN, ICON_CHECK, ICON_CROSS, ICON_HOST, ICON_SERVER,
    ICON_SPARK, ICON_HEX, ICON_STAR, ICON_DOT, ICON_RESET, ICON_GEAR, ICON_PLUS, ICON_MINUS, ICON_MUSIC, ICON_BELL,
    ICON_POWER, ICON_BRIGHT, ICON_TIMER, ICON_ALERT,
    // SPEC §6.8.9 weather set; sun and water parts keep their own colours.
    ICON_SUN, ICON_NIGHT, ICON_PARTLY, ICON_CLOUD, ICON_FOG, ICON_DRIZZLE, ICON_RAIN, ICON_HEAVY_RAIN,
    ICON_SNOW, ICON_SLEET, ICON_THUNDER, ICON_WIND,
} ui_icon_t;

// Original line icons drawn on a 24-unit grid and scaled to `size`; colour follows text_color.
lv_obj_t *ui_icon(lv_obj_t *parent, ui_icon_t icon, int size, uint32_t color);
void ui_icon_set(lv_obj_t *icon, ui_icon_t value, uint32_t color);

// Single-line label in one of the SPEC type sizes; never wraps unless given a width.
lv_obj_t *ui_text(lv_obj_t *parent, int size, uint32_t color);
void ui_text_set(lv_obj_t *label, const char *format, ...);
// One line ending in an ellipsis; the width comes from the caller (fixed, max or flex grow).
void ui_text_clip(lv_obj_t *label);
lv_obj_t *ui_box(lv_obj_t *parent, int32_t width, int32_t height);
lv_obj_t *ui_hstack(lv_obj_t *parent, int32_t gap);
lv_obj_t *ui_vstack(lv_obj_t *parent, int32_t gap);

// SPEC §6.6.3 threshold colours for a used percentage.
uint32_t ui_level_color(float used_pct);
lv_obj_t *ui_ring(lv_obj_t *parent, int32_t diameter, int32_t width);
void ui_ring_set(lv_obj_t *ring, float pct, uint32_t color);
lv_obj_t *ui_meter(lv_obj_t *parent, int32_t width);
void ui_meter_set(lv_obj_t *meter, float used_pct);
lv_obj_t *ui_chip(lv_obj_t *parent);
void ui_chip_set(lv_obj_t *chip, const char *text, uint32_t color);
lv_obj_t *ui_icon_button(lv_obj_t *parent, ui_icon_t icon, int32_t diameter, bool primary, lv_event_cb_t cb, void *data);
lv_obj_t *ui_pager(lv_obj_t *parent);
void ui_pager_set(lv_obj_t *pager, unsigned count, unsigned index);

// L1 building blocks: a dim section caption and a 72px title / value row.
lv_obj_t *ui_section(lv_obj_t *parent, const char *title);
lv_obj_t *ui_list_row(lv_obj_t *parent, const char *title, const char *value);

// Tappable horizontal row with a pressed highlight; children lay out left to right, centred vertically.
lv_obj_t *ui_row(lv_obj_t *parent, int32_t height, lv_event_cb_t cb, void *data);

// Centred placeholder for loading, unconfigured and empty states.
lv_obj_t *ui_message(lv_obj_t *parent);
void ui_message_set(lv_obj_t *message, const char *text);
