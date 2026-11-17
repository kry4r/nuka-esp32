// SPDX-License-Identifier: AGPL-3.0-or-later
#include "app_helpers.h"
#include <stdio.h>
#include <string.h>
#include <time.h>
typedef struct { lv_obj_t *row, *icon, *project, *status, *elapsed; } session_row_t;
static lv_obj_t *list, *empty;
static session_row_t rows[4];
extern unsigned buddy_selected;
static ui_icon_t source_icon(const char *source) { return !strcmp(source, "claude") ? ICON_SPARK : !strcmp(source, "codex") ? ICON_HEX : ICON_STAR; }
static const char *source_name(const char *source) { return !strcmp(source, "claude") ? "Claude" : !strcmp(source, "codex") ? "Codex" : "Gemini"; }
static const char *state_key(const char *state)
{
    return !strcmp(state, "thinking") ? "buddy.thinking" : !strcmp(state, "tool") ? "buddy.tool" : !strcmp(state, "waiting_permission") ? "buddy.waiting" :
        !strcmp(state, "done") ? "buddy.done" : !strcmp(state, "error") ? "buddy.error" : "buddy.idle";
}
static uint32_t state_color(const char *state)
{
    return !strcmp(state, "waiting_permission") ? NUKA_COLOR_WARN : !strcmp(state, "error") ? NUKA_COLOR_DANGER :
        !strcmp(state, "idle") || !strcmp(state, "done") ? NUKA_COLOR_TEXT_DIM : NUKA_COLOR_AVO;
}
// The running tool says more than "running a tool", so it replaces the generic label.
static void status_text(char *text, size_t size, const cJSON *session)
{
    const char *state = app_str(session, "state"), *tool = app_str(session, "tool");
    if (!strcmp(state, "tool") && tool) snprintf(text, size, "%s", tool); else snprintf(text, size, "%s", tr(state_key(state)));
}
static void create(lv_obj_t *card)
{
    list = app_root(card); lv_obj_set_flex_align(list, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(list, NUKA_SPACE_S, 0);
    for (unsigned i = 0; i < 4; ++i) {
        session_row_t *row = &rows[i];
        row->row = ui_box(list, LV_PCT(100), 80);
        lv_obj_set_flex_flow(row->row, LV_FLEX_FLOW_ROW); lv_obj_set_flex_align(row->row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(row->row, NUKA_SPACE_M, 0);
        row->icon = ui_icon(row->row, ICON_SPARK, 32, NUKA_COLOR_AVO);
        lv_obj_t *texts = ui_vstack(row->row, 2); lv_obj_set_flex_align(texts, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
        lv_obj_set_flex_grow(texts, 1);
        row->project = ui_text(texts, 28, NUKA_COLOR_TEXT); ui_text_clip(row->project); lv_obj_set_width(row->project, 280);
        row->status = ui_text(texts, 22, NUKA_COLOR_AVO); ui_text_clip(row->status); lv_obj_set_width(row->status, 280);
        row->elapsed = ui_text(row->row, 22, NUKA_COLOR_TEXT_DIM);
    }
    empty = ui_vstack(card, NUKA_SPACE_M); lv_obj_center(empty);
    ui_icon(empty, ICON_MOON, 64, NUKA_COLOR_AVO_DEEP);
    lv_label_set_text(ui_text(empty, 28, NUKA_COLOR_TEXT_DIM), tr("buddy.idle"));
}
static void apply(const void *vm, view_state_t state)
{
    const cJSON *sessions = cJSON_GetObjectItemCaseSensitive(vm, "sessions");
    unsigned count = cJSON_GetArraySize(sessions);
    app_show(list, count); app_show(empty, !count);
    for (unsigned i = 0; i < 4; ++i) {
        session_row_t *row = &rows[i];
        app_show(row->row, i < count);
        if (i >= count) continue;
        const cJSON *session = cJSON_GetArrayItem(sessions, (buddy_selected + i) % count);
        const char *source = app_str(session, "src"), *project = app_str(session, "project"), *state_name = app_str(session, "state");
        uint32_t color = state_color(state_name);
        ui_icon_set(row->icon, source_icon(source), color);
        ui_text_set(row->project, "%s", project ? project : source_name(source));
        char text[64]; status_text(text, sizeof(text), session); ui_text_set(row->status, "%s", text);
        lv_obj_set_style_text_color(row->status, lv_color_hex(color), 0);
        long elapsed = (long)(time(NULL) - cJSON_GetObjectItemCaseSensitive(session, "since")->valuedouble); if (elapsed < 0) elapsed = 0;
        if (elapsed >= 3600) ui_text_set(row->elapsed, "%ldh%02ld", elapsed / 3600, elapsed / 60 % 60);
        else ui_text_set(row->elapsed, "%ld:%02ld", elapsed / 60, elapsed % 60);
    }
}
static void detail(lv_obj_t *page)
{
    cJSON *vm = vm_store_snapshot(VM_AGENTS); const cJSON *session;
    cJSON_ArrayForEach(session, cJSON_GetObjectItemCaseSensitive(vm, "sessions")) {
        const char *source = app_str(session, "src"), *project = app_str(session, "project"), *host = app_str(session, "host"), *message = app_str(session, "msg");
        char title[96]; snprintf(title, sizeof(title), host ? "%s · %s" : "%s", source_name(source), host);
        ui_section(page, title);
        char text[64]; status_text(text, sizeof(text), session);
        lv_obj_t *row = ui_list_row(page, project ? project : source_name(source), text);
        lv_obj_set_style_text_color(lv_obj_get_child(row, 1), lv_color_hex(state_color(app_str(session, "state"))), 0);
        if (message) { lv_obj_t *label = ui_text(page, 22, NUKA_COLOR_TEXT_DIM); lv_obj_set_width(label, LV_PCT(100)); lv_label_set_text(label, message); }
    }
    cJSON_Delete(vm);
}
const app_view_t buddy_view = {create, apply, detail};
