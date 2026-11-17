// SPDX-License-Identifier: AGPL-3.0-or-later
#include "app_helpers.h"
#include "weather_icon.h"
#include <stdio.h>
#include <time.h>
typedef struct { lv_obj_t *column, *day, *icon, *range; } forecast_t;
static lv_obj_t *icon, *temperature, *description, *place, *stats[3];
static forecast_t days[3];
static void create(lv_obj_t *card)
{
    lv_obj_t *root = app_root(card); lv_obj_set_style_pad_row(root, NUKA_SPACE_L, 0);
    lv_obj_t *now = ui_hstack(root, NUKA_SPACE_L);
    icon = ui_icon(now, ICON_CLOUD, 112, NUKA_COLOR_TEXT);
    lv_obj_t *texts = ui_vstack(now, 0); lv_obj_set_flex_align(texts, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    temperature = ui_text(texts, 56, NUKA_COLOR_TEXT);
    description = ui_text(texts, 28, NUKA_COLOR_TEXT);
    place = ui_text(texts, 22, NUKA_COLOR_TEXT_DIM); ui_text_clip(place); lv_obj_set_style_max_width(place, 220, 0);
    lv_obj_t *row = ui_hstack(root, NUKA_SPACE_XL);
    const char *captions[] = {tr("weather.feels"), tr("weather.humidity"), tr("weather.wind")};
    for (unsigned i = 0; i < 3; ++i) stats[i] = app_stat(row, 28, captions[i]);
    lv_obj_t *forecast = ui_box(root, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(forecast, LV_FLEX_FLOW_ROW); lv_obj_set_flex_align(forecast, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_border_side(forecast, LV_BORDER_SIDE_TOP, 0); lv_obj_set_style_border_width(forecast, 1, 0);
    lv_obj_set_style_border_color(forecast, lv_color_hex(NUKA_COLOR_OUTLINE), 0); lv_obj_set_style_pad_top(forecast, NUKA_SPACE_M, 0);
    for (unsigned i = 0; i < 3; ++i) {
        days[i].column = ui_vstack(forecast, NUKA_SPACE_XS);
        days[i].day = ui_text(days[i].column, 22, NUKA_COLOR_TEXT_DIM);
        days[i].icon = ui_icon(days[i].column, ICON_CLOUD, 40, NUKA_COLOR_TEXT);
        days[i].range = ui_text(days[i].column, 22, NUKA_COLOR_TEXT);
    }
}
static void apply(const void *vm, view_state_t state)
{
    const cJSON *now = cJSON_GetObjectItemCaseSensitive(vm, "now");
    const char *icon_name = app_str(now, "icon"), *desc = app_str(now, "desc_txt"), *wind = app_str(now, "wind_txt");
    app_show(icon, icon_name); if (icon_name) ui_icon_set(icon, ui_weather_icon(icon_name), NUKA_COLOR_TEXT);
    double value;
    if (app_num(now, "temp_c", &value)) ui_text_set(temperature, "%.0f°", value);
    app_show(description, desc); if (desc) ui_text_set(description, "%s", desc);
    // SPEC §6.8.9: offline keeps the last data and says when it was taken.
    double updated; const char *location = app_str(vm, "loc_txt");
    if (state == VIEW_OFFLINE && app_num(vm, "updated", &updated)) {
        time_t at = (time_t)updated; struct tm local; localtime_r(&at, &local); char text[8]; strftime(text, sizeof(text), "%H:%M", &local);
        ui_text_set(place, "%s · %s", location, text);
    } else ui_text_set(place, "%s", location);
    bool has_feels = app_num(now, "feels_c", &value); app_show(lv_obj_get_parent(stats[0]), has_feels); if (has_feels) ui_text_set(stats[0], "%.0f°", value);
    bool has_humidity = app_num(now, "hum_pct", &value); app_show(lv_obj_get_parent(stats[1]), has_humidity); if (has_humidity) ui_text_set(stats[1], "%.0f%%", value);
    app_show(lv_obj_get_parent(stats[2]), wind); if (wind) ui_text_set(stats[2], "%s", wind);
    const cJSON *list = cJSON_GetObjectItemCaseSensitive(vm, "days");
    for (unsigned i = 0; i < 3; ++i) {
        const cJSON *day = cJSON_GetArrayItem(list, i);
        app_show(days[i].column, day);
        if (!day) continue;
        struct tm date = {0}; sscanf(app_str(day, "date"), "%d-%d-%d", &date.tm_year, &date.tm_mon, &date.tm_mday);
        date.tm_year -= 1900; date.tm_mon -= 1; date.tm_hour = 12; mktime(&date);
        char key[24]; snprintf(key, sizeof(key), "clock.weekday.%d", date.tm_wday); ui_text_set(days[i].day, "%s", tr(key));
        const char *day_icon = app_str(day, "icon"); app_show(days[i].icon, day_icon);
        if (day_icon) ui_icon_set(days[i].icon, ui_weather_icon(day_icon), NUKA_COLOR_TEXT);
        double high, low; app_num(day, "hi_c", &high); app_num(day, "lo_c", &low);
        ui_text_set(days[i].range, "%.0f° / %.0f°", high, low);
    }
}
const app_view_t weather_view = {create, apply, NULL};
