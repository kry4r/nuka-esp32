// SPDX-License-Identifier: AGPL-3.0-or-later
#include "app_helpers.h"
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <time.h>
const char *app_str(const cJSON *object, const char *key)
{
    const cJSON *value = cJSON_GetObjectItemCaseSensitive(object, key);
    return cJSON_IsString(value) ? value->valuestring : NULL;
}
bool app_num(const cJSON *object, const char *key, double *value)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(object, key);
    if (!cJSON_IsNumber(item)) return false;
    *value = item->valuedouble; return true;
}
lv_obj_t *app_root(lv_obj_t *card)
{
    lv_obj_t *root = ui_box(card, LV_PCT(100), LV_PCT(100));
    lv_obj_set_flex_flow(root, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(root, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(root, NUKA_SPACE_M, 0);
    return root;
}
lv_obj_t *app_stat(lv_obj_t *parent, int size, const char *caption)
{
    lv_obj_t *stat = ui_vstack(parent, 0);
    lv_obj_t *value = ui_text(stat, size, NUKA_COLOR_TEXT);
    lv_label_set_text(ui_text(stat, 22, NUKA_COLOR_TEXT_DIM), caption);
    return value;
}
void app_show(lv_obj_t *object, bool visible)
{
    if (lv_obj_has_flag(object, LV_OBJ_FLAG_HIDDEN) == visible) lv_obj_set_flag(object, LV_OBJ_FLAG_HIDDEN, !visible);
}
void app_countdown(char *text, size_t size, double epoch)
{
    double remain = fmax(0, epoch - time(NULL));
    if (remain >= 86400) snprintf(text, size, "%.0fd %.0fh", floor(remain / 86400), floor(fmod(remain / 3600, 24)));
    else snprintf(text, size, "%.0f:%02.0f", floor(remain / 3600), floor(fmod(remain / 60, 60)));
}
void app_detail_rows(lv_obj_t *parent, vm_topic_t topic)
{
    cJSON *data = vm_store_snapshot(topic), *row;
    if (!data) { lv_obj_t *message = ui_message(parent); ui_message_set(message, tr("state.loading")); return; }
    const cJSON *spark = cJSON_GetObjectItemCaseSensitive(data, "spark");
    if (spark) {
        static const uint32_t colors[] = {NUKA_COLOR_AVO, NUKA_COLOR_INFO, NUKA_COLOR_WARN};
        lv_obj_t *chart = lv_chart_create(parent); lv_obj_set_size(chart, LV_PCT(100), 120);
        lv_chart_set_range(chart, LV_CHART_AXIS_PRIMARY_Y, 0, 100); lv_chart_set_div_line_count(chart, 0, 0);
        const cJSON *series; unsigned index = 0;
        lv_obj_t *legend = ui_hstack(parent, NUKA_SPACE_M);
        cJSON_ArrayForEach(series, spark) {
            uint32_t color = colors[index++ % 3];
            lv_chart_set_point_count(chart, cJSON_GetArraySize(series));
            lv_chart_series_t *line = lv_chart_add_series(chart, lv_color_hex(color), LV_CHART_AXIS_PRIMARY_Y);
            const cJSON *point; cJSON_ArrayForEach(point, series) lv_chart_set_next_value(chart, line, point->valuedouble);
            lv_obj_t *key = ui_hstack(legend, NUKA_SPACE_XS);
            ui_icon(key, ICON_DOT, 14, color);
            char name[16]; snprintf(name, sizeof(name), "%s", series->string); for (char *c = name; *c; ++c) *c = (char)toupper((unsigned char)*c);
            lv_label_set_text(ui_text(key, 22, NUKA_COLOR_TEXT_DIM), name);
        }
        const char *span = app_str(data, "spark_span_txt");
        if (span) lv_label_set_text(ui_text(legend, 22, NUKA_COLOR_TEXT_DIM), span);
    }
    cJSON_ArrayForEach(row, cJSON_GetObjectItemCaseSensitive(data, "rows")) ui_list_row(parent, app_str(row, "k"), app_str(row, "v_txt"));
    cJSON_Delete(data);
}
