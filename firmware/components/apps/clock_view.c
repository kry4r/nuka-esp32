// SPDX-License-Identifier: AGPL-3.0-or-later
#include "app_helpers.h"
#include "nuka_time.h"
#include "battery.h"
#include "home_app.h"
#include "face_view.h"
#include "esp_random.h"
#include <time.h>
#include <stdio.h>
#include <string.h>
static lv_obj_t *root, *clock_text, *seconds, *date, *unsynced, *battery_ring, *next_row, *next_dot, *next_title;
extern bool clock_show_seconds;
static time_t moved;
static void create(lv_obj_t *card)
{
    // SPEC §6.8.1: the battery ring runs around the whole screen, outside the card box.
    battery_ring = ui_ring(card, 470, 4); lv_obj_align(battery_ring, LV_ALIGN_CENTER, 0, 2);
    lv_obj_set_style_arc_color(battery_ring, lv_color_hex(NUKA_COLOR_SURFACE_HI), 0);
    moved = 0;
    root = app_root(card); lv_obj_set_style_pad_row(root, NUKA_SPACE_S, 0);
    lv_obj_t *time_row = ui_hstack(root, 0); lv_obj_set_flex_align(time_row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);
    clock_text = ui_text(time_row, 112, NUKA_COLOR_TEXT);
    seconds = ui_text(time_row, 40, NUKA_COLOR_TEXT_DIM); lv_obj_set_style_pad_bottom(seconds, 18, 0);
    date = ui_text(root, 28, NUKA_COLOR_TEXT_DIM);
    unsynced = ui_text(root, 22, NUKA_COLOR_WARN); lv_label_set_text(unsynced, tr("status.unsynced"));
    next_row = ui_hstack(root, NUKA_SPACE_S); lv_obj_set_style_pad_top(next_row, NUKA_SPACE_L, 0);
    next_dot = ui_icon(next_row, ICON_DOT, 14, NUKA_COLOR_TEXT_DIM);
    next_title = ui_text(next_row, 22, NUKA_COLOR_TEXT_DIM); ui_text_clip(next_title); lv_obj_set_style_max_width(next_title, 340, 0);
    face_view_create(card);
}
static void apply(const void *vm, view_state_t state)
{
    bool assistant=home_assistant_face();app_show(root,!assistant);app_show(battery_ring,!assistant&&battery_status().batt_present);
    face_view_visible(assistant);
    if(assistant){face_view_apply();return;}
    time_t now = time(NULL); struct tm local; localtime_r(&now, &local); char text[64];
    strftime(text, sizeof(text), "%H:%M", &local); ui_text_set(clock_text, "%s", text);
    app_show(seconds, clock_show_seconds); if (clock_show_seconds) ui_text_set(seconds, ":%02d", local.tm_sec);
    static const char *weekdays[] = {"clock.weekday.0", "clock.weekday.1", "clock.weekday.2", "clock.weekday.3", "clock.weekday.4", "clock.weekday.5", "clock.weekday.6"};
    strftime(text, sizeof(text), "%m-%d", &local); ui_text_set(date, "%s  %s", tr(weekdays[local.tm_wday]), text);
    app_show(unsynced, nuka_time_stale());
    axp_status_t battery = battery_status();
    app_show(battery_ring, battery.batt_present);
    ui_ring_set(battery_ring, battery.pct, battery.charging || battery.pct >= 20 ? NUKA_COLOR_AVO : battery.pct >= 10 ? NUKA_COLOR_WARN : NUKA_COLOR_DANGER);
    cJSON *notify = vm_store_snapshot(VM_NOTIFY);
    const cJSON *first = cJSON_GetArrayItem(cJSON_GetObjectItemCaseSensitive(notify, "items"), 0);
    app_show(next_row, first);
    if (first) {
        const char *level = app_str(first, "level");
        ui_icon_set(next_dot, ICON_DOT, !strcmp(level, "critical") ? NUKA_COLOR_DANGER : !strcmp(level, "warn") ? NUKA_COLOR_WARN : NUKA_COLOR_AVO);
        ui_text_set(next_title, "%s", app_str(first, "title"));
    }
    cJSON_Delete(notify);
    // SPEC §6.8.1 burn-in guard: shift the whole face by up to 2px once a minute.
    if (now - moved >= 60) { moved = now; lv_obj_set_style_translate_x(root, (int)(esp_random() % 5) - 2, 0); lv_obj_set_style_translate_y(root, (int)(esp_random() % 5) - 2, 0); }
}
const app_view_t clock_view = {create, apply, NULL};
