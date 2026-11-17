// SPDX-License-Identifier: AGPL-3.0-or-later
#include "app_helpers.h"
#include "media_app.h"
#include <string.h>
#include <time.h>
static lv_obj_t *root, *cover, *cover_image, *cover_icon, *title, *artist, *source, *progress, *elapsed, *duration, *buttons[3], *empty;
static const char *commands[] = {"prev", "toggle", "next"};
extern const cJSON *media_current(const cJSON *vm);
extern void media_page(int direction);
static void command(lv_event_t *e) { media_command(lv_event_get_user_data(e)); }
static void next_source(lv_event_t *event) { media_page(1); }
static void create(lv_obj_t *card)
{
    root = app_root(card); lv_obj_set_style_pad_row(root, NUKA_SPACE_M, 0);
    lv_obj_t *top = ui_box(root, LV_PCT(100), 160);
    lv_obj_set_flex_flow(top, LV_FLEX_FLOW_ROW); lv_obj_set_flex_align(top, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(top, NUKA_SPACE_L, 0);
    cover = ui_box(top, 160, 160);
    lv_obj_set_style_radius(cover, NUKA_RADIUS_BTN, 0); lv_obj_set_style_clip_corner(cover, true, 0);
    lv_obj_set_style_bg_opa(cover, LV_OPA_COVER, 0); lv_obj_set_style_bg_color(cover, lv_color_hex(NUKA_COLOR_SURFACE_HI), 0);
    cover_icon = ui_icon(cover, ICON_MUSIC, 64, NUKA_COLOR_AVO_DEEP); lv_obj_center(cover_icon);
    cover_image = lv_image_create(cover); lv_obj_center(cover_image);
    lv_obj_t *texts = ui_vstack(top, NUKA_SPACE_XS); lv_obj_set_flex_align(texts, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_flex_grow(texts, 1); lv_obj_set_height(texts, 160);
    title = ui_text(texts, 28, NUKA_COLOR_TEXT); lv_obj_set_width(title, 248); lv_label_set_long_mode(title, LV_LABEL_LONG_DOT); lv_obj_set_style_max_height(title, 76, 0);
    artist = ui_text(texts, 22, NUKA_COLOR_TEXT_DIM); lv_obj_set_width(artist, 248); ui_text_clip(artist);
    source = ui_text(texts, 22, NUKA_COLOR_AVO); lv_obj_set_width(source, 248); lv_obj_set_style_pad_top(source, NUKA_SPACE_S, 0); ui_text_clip(source);
    lv_obj_add_flag(source, LV_OBJ_FLAG_CLICKABLE); lv_obj_set_ext_click_area(source, NUKA_SPACE_M);
    lv_obj_add_event_cb(source, next_source, LV_EVENT_CLICKED, NULL);
    lv_obj_t *timeline = ui_vstack(root, NUKA_SPACE_XS); lv_obj_set_width(timeline, LV_PCT(100));
    progress = ui_meter(timeline, LV_PCT(100)); lv_bar_set_range(progress, 0, 1000);
    lv_obj_set_style_bg_color(progress, lv_color_hex(NUKA_COLOR_AVO), LV_PART_INDICATOR);
    lv_obj_t *times = ui_box(timeline, LV_PCT(100), LV_SIZE_CONTENT);
    elapsed = ui_text(times, 22, NUKA_COLOR_TEXT_DIM); duration = ui_text(times, 22, NUKA_COLOR_TEXT_DIM); lv_obj_align(duration, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_t *controls = ui_hstack(root, NUKA_SPACE_XL);
    buttons[0] = ui_icon_button(controls, ICON_PREV, 64, false, command, (void *)commands[0]);
    buttons[1] = ui_icon_button(controls, ICON_PLAY, 88, true, command, (void *)commands[1]);
    buttons[2] = ui_icon_button(controls, ICON_NEXT, 64, false, command, (void *)commands[2]);
    empty = ui_vstack(card, NUKA_SPACE_M); lv_obj_center(empty);
    ui_icon(empty, ICON_MUSIC, 64, NUKA_COLOR_AVO_DEEP);
    lv_label_set_text(ui_text(empty, 28, NUKA_COLOR_TEXT_DIM), tr("media.no_track"));
}
static void apply(const void *vm, view_state_t state)
{
    const cJSON *item = media_current(vm);
    app_show(root, item); app_show(empty, !item);
    if (!item) return;
    const char *cover_path = app_str(item, "cover"), *artist_name = app_str(item, "artist"), *host = app_str(item, "host"), *app = app_str(item, "app");
    const lv_image_dsc_t *image = cover_path ? media_cover(cover_path) : NULL;
    if (lv_image_get_src(cover_image) != image) lv_image_set_src(cover_image, image);
    app_show(cover_image, image); app_show(cover_icon, !image);
    ui_text_set(title, "%s", app_str(item, "title"));
    app_show(artist, artist_name); if (artist_name) ui_text_set(artist, "%s", artist_name);
    ui_text_set(source, app ? "%s · %s" : "%s", host, app);
    bool playing = !strcmp(app_str(item, "state"), "playing");
    double position, length, at;
    bool timed = app_num(item, "pos_s", &position) && app_num(item, "dur_s", &length) && length > 0;
    app_show(lv_obj_get_parent(progress), timed);
    if (timed) {
        // PROTOCOL §3.4.7: the device advances the position locally from the last report.
        if (playing && app_num(item, "at", &at)) position += time(NULL) - at;
        position = LV_CLAMP(0, position, length);
        int32_t value = (int32_t)(1000 * position / length);
        if (lv_bar_get_value(progress) != value) lv_bar_set_value(progress, value, LV_ANIM_OFF);
        ui_text_set(elapsed, "%d:%02d", (int)position / 60, (int)position % 60);
        ui_text_set(duration, "%d:%02d", (int)length / 60, (int)length % 60);
    }
    const cJSON *can = cJSON_GetObjectItemCaseSensitive(item, "can");
    for (unsigned i = 0; i < 3; ++i) lv_obj_set_state(buttons[i], LV_STATE_DISABLED, !cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(can, commands[i])));
    ui_icon_set(lv_obj_get_child(buttons[1], 0), playing ? ICON_PAUSE : ICON_PLAY, NUKA_COLOR_BG);
}
const app_view_t media_view = {create, apply, NULL};
