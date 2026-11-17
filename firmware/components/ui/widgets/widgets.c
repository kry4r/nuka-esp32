// SPDX-License-Identifier: AGPL-3.0-or-later
#include "widgets.h"
#include "theme_tokens.h"
#include "nukapack.h"
#include "anim.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
static lv_color_t hex(uint32_t value) { return lv_color_hex(value); }
lv_obj_t *ui_text(lv_obj_t *parent, int size, uint32_t color)
{
    lv_obj_t *label = lv_label_create(parent); lv_label_set_text_static(label, "");
    lv_obj_set_style_text_font(label, size == 112 ? nuka_font(112, false) : nuka_font(size, true), 0);
    lv_obj_set_style_text_color(label, hex(color), 0);
    return label;
}
void ui_text_set(lv_obj_t *label, const char *format, ...)
{
    char text[192]; va_list args; va_start(args, format); vsnprintf(text, sizeof(text), format, args); va_end(args);
    // Unchanged text must not invalidate: data refreshes arrive every second.
    if (strcmp(lv_label_get_text(label), text)) lv_label_set_text(label, text);
}
void ui_text_clip(lv_obj_t *label)
{
    lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
    lv_obj_set_height(label, lv_font_get_line_height(lv_obj_get_style_text_font(label, 0)) + lv_obj_get_style_pad_top(label, 0) + lv_obj_get_style_pad_bottom(label, 0));
}
lv_obj_t *ui_box(lv_obj_t *parent, int32_t width, int32_t height)
{
    lv_obj_t *box = lv_obj_create(parent); lv_obj_set_size(box, width, height);
    lv_obj_remove_flag(box, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    return box;
}
static lv_obj_t *stack(lv_obj_t *parent, lv_flex_flow_t flow, int32_t gap)
{
    lv_obj_t *box = ui_box(parent, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(box, flow); lv_obj_set_flex_align(box, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_gap(box, gap, 0);
    return box;
}
lv_obj_t *ui_hstack(lv_obj_t *parent, int32_t gap) { return stack(parent, LV_FLEX_FLOW_ROW, gap); }
lv_obj_t *ui_vstack(lv_obj_t *parent, int32_t gap) { return stack(parent, LV_FLEX_FLOW_COLUMN, gap); }
uint32_t ui_level_color(float used_pct) { return used_pct > 85 ? NUKA_COLOR_DANGER : used_pct >= 60 ? NUKA_COLOR_WARN : NUKA_COLOR_AVO; }
lv_obj_t *ui_ring(lv_obj_t *parent, int32_t diameter, int32_t width)
{
    lv_obj_t *ring = lv_arc_create(parent); lv_obj_set_size(ring, diameter, diameter);
    lv_arc_set_rotation(ring, 270); lv_arc_set_bg_angles(ring, 0, 360); lv_arc_set_range(ring, 0, 1000); lv_arc_set_value(ring, 0);
    lv_obj_set_style_arc_width(ring, width, 0); lv_obj_set_style_arc_width(ring, width, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(ring, hex(NUKA_COLOR_AVO_DEEP), 0);
    lv_obj_set_style_bg_opa(ring, LV_OPA_TRANSP, LV_PART_KNOB); lv_obj_set_style_pad_all(ring, 0, LV_PART_KNOB);
    return ring;
}
void ui_ring_set(lv_obj_t *ring, float pct, uint32_t color)
{
    int32_t value = (int32_t)(LV_CLAMP(0, pct, 100) * 10);
    if (lv_arc_get_value(ring) != value) lv_arc_set_value(ring, value);
    if (!lv_color_eq(lv_obj_get_style_arc_color(ring, LV_PART_INDICATOR), hex(color))) lv_obj_set_style_arc_color(ring, hex(color), LV_PART_INDICATOR);
}
lv_obj_t *ui_meter(lv_obj_t *parent, int32_t width)
{
    lv_obj_t *meter = lv_bar_create(parent); lv_obj_set_size(meter, width, 8); lv_bar_set_range(meter, 0, 1000);
    return meter;
}
void ui_meter_set(lv_obj_t *meter, float used_pct)
{
    int32_t value = (int32_t)(LV_CLAMP(0, used_pct, 100) * 10);
    if (lv_bar_get_value(meter) != value) lv_bar_set_value(meter, value, LV_ANIM_OFF);
    uint32_t color = ui_level_color(used_pct);
    if (!lv_color_eq(lv_obj_get_style_bg_color(meter, LV_PART_INDICATOR), hex(color))) lv_obj_set_style_bg_color(meter, hex(color), LV_PART_INDICATOR);
}
lv_obj_t *ui_chip(lv_obj_t *parent)
{
    lv_obj_t *chip = ui_hstack(parent, NUKA_SPACE_S);
    lv_obj_set_style_bg_opa(chip, LV_OPA_COVER, 0); lv_obj_set_style_bg_color(chip, hex(NUKA_COLOR_SURFACE_HI), 0);
    lv_obj_set_style_radius(chip, NUKA_RADIUS_CHIP, 0); lv_obj_set_style_pad_hor(chip, 14, 0); lv_obj_set_style_pad_ver(chip, 4, 0);
    lv_obj_t *dot = ui_box(chip, 10, 10);
    lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0); lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
    ui_text(chip, 22, NUKA_COLOR_TEXT);
    return chip;
}
void ui_chip_set(lv_obj_t *chip, const char *text, uint32_t color)
{
    lv_obj_t *dot = lv_obj_get_child(chip, 0);
    if (!lv_color_eq(lv_obj_get_style_bg_color(dot, 0), hex(color))) lv_obj_set_style_bg_color(dot, hex(color), 0);
    ui_text_set(lv_obj_get_child(chip, 1), "%s", text);
}
lv_obj_t *ui_icon_button(lv_obj_t *parent, ui_icon_t icon, int32_t diameter, bool primary, lv_event_cb_t cb, void *data)
{
    lv_obj_t *button = lv_button_create(parent);
    lv_obj_set_size(button, diameter, diameter); lv_obj_set_style_radius(button, LV_RADIUS_CIRCLE, 0); lv_obj_set_style_pad_all(button, 0, 0);
    if (primary) {
        lv_obj_set_style_bg_color(button, hex(NUKA_COLOR_AVO), 0); lv_obj_set_style_bg_color(button, hex(NUKA_COLOR_AVO_SOFT), LV_STATE_PRESSED);
    }
    lv_obj_add_event_cb(button, cb, LV_EVENT_CLICKED, data);
    lv_obj_center(ui_icon(button, icon, diameter * 5 / 12, primary ? NUKA_COLOR_BG : NUKA_COLOR_TEXT));
    return button;
}
lv_obj_t *ui_pager(lv_obj_t *parent) { return ui_hstack(parent, NUKA_SPACE_S); }
void ui_pager_set(lv_obj_t *pager, unsigned count, unsigned index)
{
    static const lv_style_prop_t props[] = {LV_STYLE_WIDTH, LV_STYLE_BG_COLOR, 0};
    static lv_style_transition_dsc_t slide;
    if (!slide.props) lv_style_transition_dsc_init(&slide, props, lv_anim_path_ease_out, ANIM_BASE, 0, NULL);
    while (lv_obj_get_child_count(pager) > count) lv_obj_delete(lv_obj_get_child(pager, -1));
    while (lv_obj_get_child_count(pager) < count) {
        lv_obj_t *dot = ui_box(pager, 8, 8);
        lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0); lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
        lv_obj_set_style_transition(dot, &slide, 0);
    }
    for (unsigned i = 0; i < count; ++i) {
        lv_obj_t *dot = lv_obj_get_child(pager, i);
        lv_obj_set_width(dot, i == index ? 24 : 8);
        lv_obj_set_style_bg_color(dot, hex(i == index ? NUKA_COLOR_AVO : NUKA_COLOR_AVO_DEEP), 0);
    }
    if (count > 1) lv_obj_remove_flag(pager, LV_OBJ_FLAG_HIDDEN); else lv_obj_add_flag(pager, LV_OBJ_FLAG_HIDDEN);
}
lv_obj_t *ui_section(lv_obj_t *parent, const char *title)
{
    lv_obj_t *label = ui_text(parent, 22, NUKA_COLOR_TEXT_DIM); lv_label_set_text(label, title);
    lv_obj_set_style_pad_top(label, NUKA_SPACE_M, 0);
    return label;
}
lv_obj_t *ui_list_row(lv_obj_t *parent, const char *title, const char *value)
{
    // Long values wrap under a right-aligned column instead of losing their tail.
    lv_obj_t *row = ui_box(parent, LV_PCT(100), LV_SIZE_CONTENT); lv_obj_set_style_min_height(row, 72, 0); lv_obj_set_style_pad_ver(row, NUKA_SPACE_S, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW); lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row, NUKA_SPACE_M, 0);
    lv_obj_set_style_border_side(row, LV_BORDER_SIDE_BOTTOM, 0); lv_obj_set_style_border_width(row, 1, 0);
    lv_obj_set_style_border_color(row, hex(NUKA_COLOR_OUTLINE), 0);
    lv_obj_t *label = ui_text(row, 28, NUKA_COLOR_TEXT); lv_label_set_text(label, title);
    ui_text_clip(label); lv_obj_set_flex_grow(label, 1);
    label = ui_text(row, 28, NUKA_COLOR_TEXT_DIM); lv_label_set_text(label, value);
    lv_obj_set_style_max_width(label, 260, 0); lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_RIGHT, 0);
    return row;
}
lv_obj_t *ui_row(lv_obj_t *parent, int32_t height, lv_event_cb_t cb, void *data)
{
    lv_obj_t *row = ui_box(parent, LV_PCT(100), height);
    lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLL_ON_FOCUS);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW); lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_hor(row, NUKA_SPACE_M, 0); lv_obj_set_style_pad_column(row, NUKA_SPACE_M, 0);
    lv_obj_set_style_radius(row, NUKA_RADIUS_BTN, 0);
    lv_obj_set_style_bg_color(row, hex(NUKA_COLOR_SURFACE_HI), LV_STATE_PRESSED); lv_obj_set_style_bg_opa(row, LV_OPA_COVER, LV_STATE_PRESSED);
    lv_obj_add_event_cb(row, cb, LV_EVENT_CLICKED, data);
    return row;
}
lv_obj_t *ui_message(lv_obj_t *parent)
{
    lv_obj_t *label = ui_text(parent, 28, NUKA_COLOR_TEXT_DIM);
    lv_obj_set_width(label, 360); lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0); lv_obj_center(label);
    return label;
}
void ui_message_set(lv_obj_t *message, const char *text) { ui_text_set(message, "%s", text); }
