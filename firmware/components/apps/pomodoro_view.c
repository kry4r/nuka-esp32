// SPDX-License-Identifier: AGPL-3.0-or-later
#include "app_helpers.h"
#include "pomodoro_app.h"
#include "overlay.h"
#include "esp_log.h"
#include <stdio.h>
#include <string.h>
extern const app_def_t pomodoro_app;
static lv_obj_t *ring, *phase, *remaining, *dots, *toggle;
static lv_obj_t *duration_labels[3];
static unsigned durations[3];
static void action(lv_event_t *e) { pomodoro_app.on_action((app_action_t)(intptr_t)lv_event_get_user_data(e)); nav_refresh(); }
static void settings(lv_event_t *e) { nav_detail(); }
static void create(lv_obj_t *card)
{
    lv_obj_t *root = app_root(card); lv_obj_set_style_pad_row(root, NUKA_SPACE_M, 0);
    ring = ui_ring(root, 236, 14);
    lv_obj_t *center = ui_vstack(ring, NUKA_SPACE_XS); lv_obj_center(center);
    phase = ui_text(center, 28, NUKA_COLOR_AVO);
    remaining = ui_text(center, 56, NUKA_COLOR_TEXT);
    dots = ui_hstack(center, NUKA_SPACE_S); lv_obj_set_style_pad_top(dots, NUKA_SPACE_XS, 0);
    for (unsigned i = 0; i < 4; ++i) ui_icon(dots, ICON_DOT, 14, NUKA_COLOR_AVO_DEEP);
    lv_obj_t *controls = ui_hstack(root, NUKA_SPACE_L);
    ui_icon_button(controls, ICON_RESET, 64, false, action, (void *)ACT_SECONDARY);
    toggle = ui_icon_button(controls, ICON_PLAY, 88, true, action, (void *)ACT_PRIMARY);
    ui_icon_button(controls, ICON_GEAR, 64, false, settings, NULL);
}
static void apply(const void *vm, view_state_t state)
{
    pomodoro_status_t status = pomodoro_status();
    unsigned index = !strcmp(status.phase, "focus") ? 0 : !strcmp(status.phase, "long_break") ? 2 : 1;
    static const char *names[] = {"pomodoro.focus", "pomodoro.break", "pomodoro.long"};
    // Focus is the accent; breaks read as calm information.
    uint32_t color = index ? NUKA_COLOR_INFO : NUKA_COLOR_AVO;
    ui_text_set(phase, "%s", tr(names[index])); lv_obj_set_style_text_color(phase, lv_color_hex(color), 0);
    ui_text_set(remaining, "%02u:%02u", status.remaining / 60, status.remaining % 60);
    ui_ring_set(ring, 100.0f * status.remaining / (status.minutes[index] * 60), color);
    unsigned done = (status.round - 1) % 4;
    for (unsigned i = 0; i < 4; ++i) ui_icon_set(lv_obj_get_child(dots, i), ICON_DOT, i <= done ? color : NUKA_COLOR_AVO_DEEP);
    ui_icon_set(lv_obj_get_child(toggle, 0), status.running ? ICON_PAUSE : ICON_PLAY, NUKA_COLOR_BG);
}
static void adjust(lv_event_t *event)
{
    unsigned value = (uintptr_t)lv_event_get_user_data(event), index = value / 2;
    if (value % 2 && durations[index] < 180) ++durations[index];
    if (!(value % 2) && durations[index] > 1) --durations[index];
    ui_text_set(duration_labels[index], "%u", durations[index]);
}
static void save(lv_event_t *event)
{
    esp_err_t error = pomodoro_set_minutes(durations[0], durations[1], durations[2]);
    if (error != ESP_OK) ESP_LOGE("pomo", "save: %s", esp_err_to_name(error));
    overlay_toast(tr(error == ESP_OK ? "vault.saved" : "vault.failed"));
}
static void detail(lv_obj_t *page)
{
    pomodoro_status_t status = pomodoro_status();
    ui_section(page, tr("pomodoro.duration"));
    static const char *labels[] = {"pomodoro.focus", "pomodoro.break", "pomodoro.long"};
    for (unsigned i = 0; i < 3; ++i) {
        durations[i] = status.minutes[i];
        lv_obj_t *row = ui_box(page, LV_PCT(100), 80);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW); lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(row, NUKA_SPACE_M, 0);
        lv_obj_t *label = ui_text(row, 28, NUKA_COLOR_TEXT); lv_label_set_text(label, tr(labels[i])); lv_obj_set_flex_grow(label, 1);
        lv_group_add_obj(nav_focus_group(), ui_icon_button(row, ICON_MINUS, 64, false, adjust, (void *)(uintptr_t)(i * 2)));
        duration_labels[i] = ui_text(row, 40, NUKA_COLOR_TEXT); lv_obj_set_width(duration_labels[i], 72);
        lv_obj_set_style_text_align(duration_labels[i], LV_TEXT_ALIGN_CENTER, 0); ui_text_set(duration_labels[i], "%u", durations[i]);
        lv_group_add_obj(nav_focus_group(), ui_icon_button(row, ICON_PLUS, 64, false, adjust, (void *)(uintptr_t)(i * 2 + 1)));
    }
    lv_obj_t *button = ui_button_primary(page, tr("action.save"), save, NULL); lv_obj_set_style_margin_top(button, NUKA_SPACE_M, 0);
    lv_group_add_obj(nav_focus_group(), button);
}
const app_view_t pomodoro_view = {create, apply, detail};
