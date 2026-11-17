// SPDX-License-Identifier: AGPL-3.0-or-later
#include "app_helpers.h"
extern unsigned servers_selected;
typedef struct { lv_obj_t *dot, *name, *meters, *bars[3], *latency, *alerts, *alert_text; } server_row_t;
static lv_obj_t *list, *empty;
static server_row_t rows[12];
static unsigned row_count;
static void select_server(lv_event_t *event) { servers_selected = (uintptr_t)lv_event_get_user_data(event); nav_detail(); }
static void create(lv_obj_t *card)
{
    list = ui_box(card, LV_PCT(100), LV_PCT(100)); lv_obj_add_flag(list, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(list, LV_DIR_VER); lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    row_count = 0;
    empty = ui_message(card); ui_message_set(empty, tr("state.empty"));
}
static void add_row(unsigned index)
{
    server_row_t *row = &rows[index];
    lv_obj_t *box = ui_row(list, 58, select_server, (void *)(uintptr_t)index);
    row->dot = ui_icon(box, ICON_DOT, 16, NUKA_COLOR_AVO);
    row->name = ui_text(box, 28, NUKA_COLOR_TEXT); ui_text_clip(row->name); lv_obj_set_flex_grow(row->name, 1);
    row->alerts = ui_hstack(box, 0);
    lv_obj_set_style_bg_opa(row->alerts, LV_OPA_COVER, 0); lv_obj_set_style_bg_color(row->alerts, lv_color_hex(NUKA_COLOR_DANGER), 0);
    lv_obj_set_style_radius(row->alerts, NUKA_RADIUS_CHIP, 0); lv_obj_set_style_pad_hor(row->alerts, 8, 0); lv_obj_set_style_min_width(row->alerts, 28, 0);
    row->alert_text = ui_text(row->alerts, 22, NUKA_COLOR_BG);
    // CPU / memory / disk as three stacked hairline meters keep the row to one line of text.
    row->meters = ui_vstack(box, 5);
    for (unsigned i = 0; i < 3; ++i) { row->bars[i] = ui_meter(row->meters, 72); lv_obj_set_height(row->bars[i], 5); }
    row->latency = ui_text(box, 22, NUKA_COLOR_TEXT_DIM);
}
static void apply(const void *vm, view_state_t state)
{
    const cJSON *items = cJSON_GetObjectItemCaseSensitive(vm, "items"), *item;
    unsigned count = cJSON_GetArraySize(items);
    if (count != row_count) { lv_obj_clean(list); for (row_count = 0; row_count < count; ++row_count) add_row(row_count); }
    app_show(list, count); app_show(empty, !count);
    unsigned index = 0;
    cJSON_ArrayForEach(item, items) {
        server_row_t *row = &rows[index++];
        bool online = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(item, "online"));
        ui_icon_set(row->dot, ICON_DOT, online ? NUKA_COLOR_AVO : NUKA_COLOR_DANGER);
        ui_text_set(row->name, "%s", app_str(item, "name"));
        static const char *keys[] = {"cpu_pct", "mem_pct", "disk_pct"};
        bool metrics = false;
        for (unsigned i = 0; i < 3; ++i) { double value; bool has = app_num(item, keys[i], &value); metrics |= has; ui_meter_set(row->bars[i], has ? value : 0); }
        app_show(row->meters, metrics && online);
        double latency; bool has_latency = app_num(item, "latency_ms", &latency);
        app_show(row->latency, !metrics && has_latency);
        if (has_latency) ui_text_set(row->latency, "%.0f ms", latency);
        double alerts; bool alerting = app_num(item, "alerts", &alerts) && alerts > 0;
        app_show(row->alerts, alerting);
        if (alerting) ui_text_set(row->alert_text, "%.0f", alerts);
    }
}
static void detail(lv_obj_t *page) { app_detail_rows(page, VM_SERVER_DETAIL); }
const app_view_t servers_view = {create, apply, detail};
