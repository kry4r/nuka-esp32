// SPDX-License-Identifier: AGPL-3.0-or-later
#include "app_helpers.h"
#include <stdio.h>
extern unsigned hosts_selected;
typedef struct { lv_obj_t *group, *load, *load_text, *temp, *temp_text; } chip_rings_t;
static lv_obj_t *root, *name, *status_dot, *pager, *mem_row, *mem_meter, *mem_text, *net_row, *up_text, *down_text, *empty;
static chip_rings_t chips[2];
static void next_host(lv_event_t *event) { extern const app_def_t hosts_app; hosts_app.on_action(ACT_PRIMARY); nav_refresh(); }
static lv_obj_t *ring(lv_obj_t *parent, lv_obj_t **text)
{
    lv_obj_t *ring = ui_ring(parent, 92, 10);
    *text = ui_text(ring, 28, NUKA_COLOR_TEXT); lv_obj_center(*text);
    return ring;
}
static void chip_create(chip_rings_t *chip, lv_obj_t *parent, const char *caption)
{
    chip->group = ui_vstack(parent, NUKA_SPACE_S);
    lv_obj_t *pair = ui_hstack(chip->group, NUKA_SPACE_S);
    chip->load = ring(pair, &chip->load_text); chip->temp = ring(pair, &chip->temp_text);
    lv_label_set_text(ui_text(chip->group, 22, NUKA_COLOR_TEXT_DIM), caption);
}
// SPEC §6.8.2: a missing metric hides its control rather than showing 0.
static void chip_apply(chip_rings_t *chip, const cJSON *data)
{
    double load, temp; bool has_load = app_num(data, "pct", &load), has_temp = app_num(data, "temp_c", &temp);
    app_show(chip->group, has_load || has_temp);
    app_show(chip->load, has_load); app_show(chip->temp, has_temp);
    if (has_load) { ui_ring_set(chip->load, load, ui_level_color(load)); ui_text_set(chip->load_text, "%.0f%%", load); }
    if (has_temp) { ui_ring_set(chip->temp, temp, ui_level_color(temp)); ui_text_set(chip->temp_text, "%.0f°", temp); }
}
static void create(lv_obj_t *card)
{
    root = app_root(card); lv_obj_set_flex_align(root, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(root, NUKA_SPACE_L, 0);
    lv_obj_t *header = ui_box(root, LV_PCT(100), 40);
    lv_obj_set_flex_flow(header, LV_FLEX_FLOW_ROW); lv_obj_set_flex_align(header, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(header, NUKA_SPACE_S, 0);
    lv_obj_add_flag(header, LV_OBJ_FLAG_CLICKABLE); lv_obj_add_event_cb(header, next_host, LV_EVENT_CLICKED, NULL);
    status_dot = ui_icon(header, ICON_DOT, 16, NUKA_COLOR_AVO);
    name = ui_text(header, 32, NUKA_COLOR_TEXT); ui_text_clip(name); lv_obj_set_flex_grow(name, 1);
    pager = ui_pager(header);
    lv_obj_t *rings = ui_hstack(root, NUKA_SPACE_L);
    chip_create(&chips[0], rings, "CPU"); chip_create(&chips[1], rings, "GPU");
    mem_row = ui_box(root, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(mem_row, LV_FLEX_FLOW_ROW); lv_obj_set_flex_align(mem_row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(mem_row, NUKA_SPACE_M, 0);
    lv_label_set_text(ui_text(mem_row, 22, NUKA_COLOR_TEXT_DIM), tr("metric.memory"));
    mem_meter = ui_meter(mem_row, 10); lv_obj_set_flex_grow(mem_meter, 1);
    mem_text = ui_text(mem_row, 22, NUKA_COLOR_TEXT);
    net_row = ui_hstack(root, NUKA_SPACE_S);
    ui_icon(net_row, ICON_UP, 20, NUKA_COLOR_TEXT_DIM); up_text = ui_text(net_row, 22, NUKA_COLOR_TEXT);
    lv_obj_set_style_pad_right(up_text, NUKA_SPACE_L, 0);
    ui_icon(net_row, ICON_DOWN, 20, NUKA_COLOR_TEXT_DIM); down_text = ui_text(net_row, 22, NUKA_COLOR_TEXT);
    empty = ui_message(card); ui_message_set(empty, tr("state.empty"));
}
static void apply(const void *vm, view_state_t state)
{
    const cJSON *items = cJSON_GetObjectItemCaseSensitive(vm, "items"); unsigned count = cJSON_GetArraySize(items);
    app_show(root, count); app_show(empty, !count);
    if (!count) return;
    const cJSON *host = cJSON_GetArrayItem(items, hosts_selected % count);
    ui_text_set(name, "%s", app_str(host, "name"));
    ui_icon_set(status_dot, ICON_DOT, cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(host, "online")) ? NUKA_COLOR_AVO : NUKA_COLOR_DANGER);
    ui_pager_set(pager, count, hosts_selected % count);
    chip_apply(&chips[0], cJSON_GetObjectItemCaseSensitive(host, "cpu"));
    chip_apply(&chips[1], cJSON_GetObjectItemCaseSensitive(host, "gpu"));
    double mem; bool has_mem = app_num(cJSON_GetObjectItemCaseSensitive(host, "mem"), "pct", &mem);
    app_show(mem_row, has_mem);
    if (has_mem) { ui_meter_set(mem_meter, mem); ui_text_set(mem_text, "%.0f%%", mem); }
    const cJSON *net = cJSON_GetObjectItemCaseSensitive(host, "net");
    app_show(net_row, net);
    if (net) { ui_text_set(up_text, "%s", app_str(net, "up_txt")); ui_text_set(down_text, "%s", app_str(net, "down_txt")); }
}
static void detail(lv_obj_t *page) { app_detail_rows(page, VM_HOST_DETAIL); }
const app_view_t hosts_view = {create, apply, detail};
