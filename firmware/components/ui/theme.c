// SPDX-License-Identifier: AGPL-3.0-or-later
#include "app.h"
#include "theme.h"
#include "anim.h"
#include "nukapack.h"
#include "i18n.h"
#include <stdio.h>
static lv_style_t base, scrollbar, button, button_pressed, button_disabled, focus_ring, track, indicator, knob, field, list_box, list_selected, check_box, check_on, chart;
static const lv_style_prop_t press_props[] = {LV_STYLE_TRANSFORM_SCALE_X, LV_STYLE_TRANSFORM_SCALE_Y, LV_STYLE_BG_COLOR, 0};
static lv_style_transition_dsc_t press_transition;
static lv_color_t hex(uint32_t value) { return lv_color_hex(value); }
static void init_styles(void)
{
    lv_style_init(&base);
    lv_style_set_bg_opa(&base, LV_OPA_TRANSP); lv_style_set_border_width(&base, 0); lv_style_set_pad_all(&base, 0);
    lv_style_set_radius(&base, 0); lv_style_set_text_color(&base, hex(NUKA_COLOR_TEXT));
    lv_style_init(&scrollbar);
    lv_style_set_width(&scrollbar, 4); lv_style_set_bg_opa(&scrollbar, LV_OPA_COVER); lv_style_set_bg_color(&scrollbar, hex(NUKA_COLOR_AVO_DEEP));
    lv_style_set_radius(&scrollbar, 2); lv_style_set_pad_right(&scrollbar, 2);
    lv_style_transition_dsc_init(&press_transition, press_props, lv_anim_path_ease_out, ANIM_PRESS, 0, NULL);
    lv_style_init(&button);
    lv_style_set_bg_opa(&button, LV_OPA_COVER); lv_style_set_bg_color(&button, hex(NUKA_COLOR_SURFACE_HI));
    lv_style_set_radius(&button, NUKA_RADIUS_CHIP); lv_style_set_pad_hor(&button, NUKA_SPACE_L); lv_style_set_pad_ver(&button, NUKA_SPACE_M);
    lv_style_set_text_color(&button, hex(NUKA_COLOR_TEXT)); lv_style_set_transition(&button, &press_transition);
    lv_style_init(&button_pressed);
    lv_style_set_bg_color(&button_pressed, hex(NUKA_COLOR_OUTLINE));
    lv_style_set_transform_scale(&button_pressed, 246);
    lv_style_init(&button_disabled);
    lv_style_set_opa(&button_disabled, LV_OPA_30);
    lv_style_init(&focus_ring);
    lv_style_set_outline_width(&focus_ring, 3); lv_style_set_outline_color(&focus_ring, hex(NUKA_COLOR_AVO_SOFT)); lv_style_set_outline_pad(&focus_ring, 3);
    lv_style_init(&track);
    lv_style_set_bg_opa(&track, LV_OPA_COVER); lv_style_set_bg_color(&track, hex(NUKA_COLOR_SURFACE_HI)); lv_style_set_radius(&track, LV_RADIUS_CIRCLE);
    lv_style_set_arc_color(&track, hex(NUKA_COLOR_SURFACE_HI)); lv_style_set_arc_width(&track, 14); lv_style_set_arc_rounded(&track, true);
    lv_style_init(&indicator);
    lv_style_set_bg_opa(&indicator, LV_OPA_COVER); lv_style_set_bg_color(&indicator, hex(NUKA_COLOR_AVO)); lv_style_set_radius(&indicator, LV_RADIUS_CIRCLE);
    lv_style_set_arc_color(&indicator, hex(NUKA_COLOR_AVO)); lv_style_set_arc_width(&indicator, 14); lv_style_set_arc_rounded(&indicator, true);
    lv_style_init(&knob);
    lv_style_set_bg_opa(&knob, LV_OPA_COVER); lv_style_set_bg_color(&knob, hex(NUKA_COLOR_TEXT)); lv_style_set_radius(&knob, LV_RADIUS_CIRCLE); lv_style_set_pad_all(&knob, 8);
    lv_style_init(&field);
    lv_style_set_bg_opa(&field, LV_OPA_COVER); lv_style_set_bg_color(&field, hex(NUKA_COLOR_SURFACE_HI)); lv_style_set_radius(&field, NUKA_RADIUS_BTN);
    lv_style_set_pad_hor(&field, NUKA_SPACE_L); lv_style_set_pad_ver(&field, NUKA_SPACE_M); lv_style_set_text_color(&field, hex(NUKA_COLOR_TEXT));
    lv_style_init(&list_box);
    lv_style_set_bg_opa(&list_box, LV_OPA_COVER); lv_style_set_bg_color(&list_box, hex(NUKA_COLOR_SURFACE)); lv_style_set_radius(&list_box, NUKA_RADIUS_BTN);
    lv_style_set_border_width(&list_box, 2); lv_style_set_border_color(&list_box, hex(NUKA_COLOR_OUTLINE)); lv_style_set_pad_all(&list_box, NUKA_SPACE_S);
    lv_style_set_text_line_space(&list_box, NUKA_SPACE_L);
    lv_style_init(&list_selected);
    lv_style_set_bg_opa(&list_selected, LV_OPA_COVER); lv_style_set_bg_color(&list_selected, hex(NUKA_COLOR_AVO_DEEP)); lv_style_set_text_color(&list_selected, hex(NUKA_COLOR_TEXT));
    lv_style_init(&check_box);
    lv_style_set_border_width(&check_box, 2); lv_style_set_border_color(&check_box, hex(NUKA_COLOR_OUTLINE)); lv_style_set_radius(&check_box, 8); lv_style_set_pad_all(&check_box, 4);
    lv_style_set_bg_opa(&check_box, LV_OPA_COVER); lv_style_set_bg_color(&check_box, hex(NUKA_COLOR_BG));
    lv_style_init(&check_on);
    lv_style_set_bg_color(&check_on, hex(NUKA_COLOR_AVO)); lv_style_set_border_color(&check_on, hex(NUKA_COLOR_AVO)); lv_style_set_text_color(&check_on, hex(NUKA_COLOR_BG));
    lv_style_init(&chart);
    lv_style_set_line_width(&chart, 3); lv_style_set_line_color(&chart, hex(NUKA_COLOR_OUTLINE)); lv_style_set_size(&chart, 0, 0);
}
static void apply(lv_theme_t *theme, lv_obj_t *obj)
{
    LV_UNUSED(theme);
    lv_obj_add_style(obj, &base, 0);
    lv_obj_add_style(obj, &scrollbar, LV_PART_SCROLLBAR);
    lv_obj_set_scrollbar_mode(obj, LV_SCROLLBAR_MODE_ACTIVE);
    if (lv_obj_check_type(obj, &lv_button_class)) {
        lv_obj_add_style(obj, &button, 0); lv_obj_add_style(obj, &button_pressed, LV_STATE_PRESSED); lv_obj_add_style(obj, &button_disabled, LV_STATE_DISABLED);
        lv_obj_add_style(obj, &focus_ring, LV_STATE_FOCUS_KEY);
        // Touch acts directly; only the key group moves focus, so taps leave no lingering ring.
        lv_obj_remove_flag(obj, LV_OBJ_FLAG_CLICK_FOCUSABLE);
    } else if (lv_obj_check_type(obj, &lv_arc_class)) {
        lv_obj_add_style(obj, &track, 0); lv_obj_set_style_bg_opa(obj, LV_OPA_TRANSP, 0);
        lv_obj_add_style(obj, &indicator, LV_PART_INDICATOR);
        lv_obj_remove_flag(obj, LV_OBJ_FLAG_CLICKABLE);
    } else if (lv_obj_check_type(obj, &lv_bar_class)) {
        lv_obj_add_style(obj, &track, 0); lv_obj_add_style(obj, &indicator, LV_PART_INDICATOR);
    } else if (lv_obj_check_type(obj, &lv_slider_class)) {
        lv_obj_add_style(obj, &track, 0); lv_obj_add_style(obj, &indicator, LV_PART_INDICATOR);
        lv_obj_add_style(obj, &knob, LV_PART_KNOB); lv_obj_add_style(obj, &focus_ring, LV_STATE_FOCUS_KEY | LV_PART_KNOB);
        lv_obj_remove_flag(obj, LV_OBJ_FLAG_CLICK_FOCUSABLE);
    } else if (lv_obj_check_type(obj, &lv_dropdown_class) || lv_obj_check_type(obj, &lv_spinbox_class) || lv_obj_check_type(obj, &lv_textarea_class)) {
        lv_obj_add_style(obj, &field, 0); lv_obj_add_style(obj, &focus_ring, LV_STATE_FOCUS_KEY);
        lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, LV_PART_CURSOR); lv_obj_set_style_bg_color(obj, hex(NUKA_COLOR_AVO), LV_PART_CURSOR);
        lv_obj_remove_flag(obj, LV_OBJ_FLAG_CLICK_FOCUSABLE);
    } else if (lv_obj_check_type(obj, &lv_dropdownlist_class)) {
        lv_obj_add_style(obj, &list_box, 0); lv_obj_add_style(obj, &list_selected, LV_PART_SELECTED | LV_STATE_CHECKED);
        lv_obj_add_style(obj, &list_selected, LV_PART_SELECTED | LV_STATE_PRESSED);
    } else if (lv_obj_check_type(obj, &lv_checkbox_class)) {
        lv_obj_set_style_pad_column(obj, NUKA_SPACE_M, 0);
        lv_obj_add_style(obj, &check_box, LV_PART_INDICATOR); lv_obj_add_style(obj, &check_on, LV_PART_INDICATOR | LV_STATE_CHECKED);
        lv_obj_add_style(obj, &focus_ring, LV_STATE_FOCUS_KEY); lv_obj_remove_flag(obj, LV_OBJ_FLAG_CLICK_FOCUSABLE);
    } else if (lv_obj_check_type(obj, &lv_chart_class)) {
        lv_obj_add_style(obj, &chart, LV_PART_ITEMS); lv_obj_set_style_size(obj, 0, 0, LV_PART_INDICATOR);
        lv_obj_set_style_line_opa(obj, LV_OPA_TRANSP, 0);
    }
}
void theme_init(void)
{
    init_styles();
    lv_theme_t *theme = lv_theme_create(); lv_theme_set_apply_cb(theme, apply);
    lv_display_set_theme(lv_display_get_default(), theme);
    lv_obj_t *screen = lv_screen_active();
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(screen, hex(NUKA_COLOR_BG), 0);
    lv_obj_set_style_text_color(screen, hex(NUKA_COLOR_TEXT), 0);
    lv_obj_set_style_text_font(screen, nuka_font(28, true), 0);
    lv_obj_set_style_pad_all(screen, 0, 0);
    lv_obj_remove_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
}
lv_obj_t *ui_label(lv_obj_t *parent, const char *text, int size)
{
    lv_obj_t *label = lv_label_create(parent); lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, nuka_font(size, true), 0);
    lv_obj_set_width(label, LV_PCT(100));
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    return label;
}
lv_obj_t *ui_button(lv_obj_t *parent, const char *text, lv_event_cb_t cb, void *data)
{
    lv_obj_t *button = lv_button_create(parent);
    lv_obj_set_size(button, LV_PCT(100), 80);
    lv_obj_add_event_cb(button, cb, LV_EVENT_CLICKED, data);
    lv_obj_t *label = lv_label_create(button); lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, nuka_font(28, true), 0);
    lv_label_set_long_mode(label, LV_LABEL_LONG_DOT); lv_obj_set_style_max_width(label, LV_PCT(100), 0); lv_obj_center(label);
    return button;
}
lv_obj_t *ui_button_primary(lv_obj_t *parent, const char *text, lv_event_cb_t cb, void *data)
{
    lv_obj_t *button = ui_button(parent, text, cb, data);
    lv_obj_set_style_bg_color(button, hex(NUKA_COLOR_AVO), 0); lv_obj_set_style_bg_color(button, hex(NUKA_COLOR_AVO_SOFT), LV_STATE_PRESSED);
    lv_obj_set_style_text_color(lv_obj_get_child(button, 0), hex(NUKA_COLOR_BG), 0);
    return button;
}
void ui_column(lv_obj_t *parent)
{
    lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(parent, NUKA_SPACE_M, 0);
}
