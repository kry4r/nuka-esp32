// SPDX-License-Identifier: AGPL-3.0-or-later
#include "app_helpers.h"
#include "anim.h"
#include "hub_client.h"
#include <stdio.h>
#include <string.h>
extern const cJSON *ai_current(const cJSON *vm);
static lv_obj_t *root, *provider, *label, *status, *outer, *inner, *center_value, *center_caption;
static lv_obj_t *reset_row, *reset_icon, *reset_text, *summary, *stats[3], *stat_cells[3], *empty, *account_pager;
static int32_t displayed_pct, target_pct;
static bool first_value;
static void stat_position(void *object,int32_t value) { lv_obj_set_style_translate_y(object,value,0); }
static void stat_set(lv_obj_t *object,const char *text)
{
    if(!strcmp(lv_label_get_text(object),text))return;
    ui_text_set(object,"%s",text);
    lv_anim_t anim;lv_anim_init(&anim);lv_anim_set_var(&anim,object);lv_anim_set_exec_cb(&anim,stat_position);
    lv_anim_set_values(&anim,8,0);lv_anim_set_duration(&anim,ANIM_BASE);lv_anim_set_path_cb(&anim,lv_anim_path_ease_out);lv_anim_start(&anim);
}
static ui_icon_t provider_icon(const char *kind)
{
    return !strcmp(kind, "claude") ? ICON_SPARK : !strcmp(kind, "codex") ? ICON_HEX : !strcmp(kind, "antigravity") ? ICON_STAR : ICON_DOT;
}
static uint32_t status_color(const char *status)
{
    return !strcmp(status, "ok") ? NUKA_COLOR_AVO : !strcmp(status, "cooldown") ? NUKA_COLOR_WARN :
        !strcmp(status, "exhausted") || !strcmp(status, "reauth") || !strcmp(status, "breaker") ? NUKA_COLOR_DANGER : NUKA_COLOR_TEXT_DIM;
}
static const char *account_status(const cJSON *account)
{
    return cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(account, "breaker_open")) ? "breaker" : app_str(account, "status");
}
static const char *window_label(const cJSON *window)
{
    const char *label = app_str(window, "label"); if (label) return label;
    char key[40]; snprintf(key, sizeof(key), "ai.window.%s", app_str(window, "kind")); return tr(key);
}
static const cJSON *find_window(const cJSON *account, const char *kind)
{
    const cJSON *window;
    cJSON_ArrayForEach(window, cJSON_GetObjectItemCaseSensitive(account, "windows")) if (!strcmp(app_str(window, "kind"), kind)) return window;
    return NULL;
}
static void percentage(void *object, int32_t value)
{
    displayed_pct = value; ui_text_set(object, "%.0f%%", value / 10.0);
}
static void animate_percentage(int32_t value)
{
    if (first_value) { percentage(center_value, value); first_value = false; target_pct = value; return; }
    if (value == target_pct) return;
    target_pct = value;
    lv_anim_t anim; lv_anim_init(&anim); lv_anim_set_var(&anim, center_value); lv_anim_set_exec_cb(&anim, percentage);
    lv_anim_set_values(&anim, displayed_pct, value); lv_anim_set_duration(&anim, ANIM_BASE);
    lv_anim_set_path_cb(&anim, lv_anim_path_ease_out); lv_anim_start(&anim);
}
static void create(lv_obj_t *card)
{
    root = app_root(card); lv_obj_set_width(root, 384); lv_obj_center(root); lv_obj_set_style_pad_row(root, NUKA_SPACE_S, 0);
    lv_obj_t *header = ui_box(root, LV_PCT(100), 34);
    lv_obj_set_flex_flow(header, LV_FLEX_FLOW_ROW); lv_obj_set_flex_align(header, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(header, NUKA_SPACE_S, 0);
    provider = ui_icon(header, ICON_DOT, 26, NUKA_COLOR_AVO);
    label = ui_text(header, 22, NUKA_COLOR_TEXT); ui_text_clip(label); lv_obj_set_flex_grow(label, 1);
    status = ui_chip(header); lv_obj_set_style_pad_hor(status, NUKA_SPACE_S, 0); lv_obj_set_style_pad_column(status, NUKA_SPACE_XS, 0);
    // Both rings show what remains; an omitted window never looks like a full quota.
    outer = ui_ring(root, 200, 14);
    inner = ui_ring(outer, 160, 10); lv_obj_center(inner);
    lv_obj_t *center = ui_vstack(outer, 0); lv_obj_center(center);
    center_value = ui_text(center, 40, NUKA_COLOR_TEXT);
    center_caption = ui_text(center, 22, NUKA_COLOR_TEXT_DIM); first_value = true;
    reset_row = ui_hstack(root, NUKA_SPACE_S);
    reset_icon = ui_icon(reset_row, ICON_RESET, 20, NUKA_COLOR_TEXT_DIM); reset_text = ui_text(reset_row, 22, NUKA_COLOR_TEXT_DIM);
    summary = ui_hstack(root, 0); lv_obj_set_width(summary, LV_PCT(100));
    lv_obj_set_flex_align(summary, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    const char *captions[] = {tr("ai.requests"), tr("ai.tokens"), tr("ai.failures")};
    for (unsigned i = 0; i < 3; ++i) {
        stat_cells[i]=ui_vstack(summary,0);
        lv_obj_t *slot=ui_box(stat_cells[i],116,36);
        stats[i]=ui_text(slot,28,NUKA_COLOR_TEXT);lv_obj_center(stats[i]);
        lv_label_set_text(ui_text(stat_cells[i],22,NUKA_COLOR_TEXT_DIM),captions[i]);
    }
    account_pager = ui_pager(root);
    empty = ui_message(card); ui_message_set(empty, tr("state.empty"));
}
static void ring_apply(lv_obj_t *ring, const cJSON *window)
{
    app_show(ring, window);
    if (!window) return;
    double used = cJSON_GetObjectItemCaseSensitive(window, "used_pct")->valuedouble;
    ui_ring_set(ring, 100 - used, cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(window, "stale")) ? NUKA_COLOR_TEXT_DIM : ui_level_color(used));
}
static double recovery_time(const cJSON *account, bool *recovering)
{
    double when = 0, recover; const cJSON *window;
    *recovering = app_num(account, "recover_at", &recover);
    if (*recovering) return recover;
    cJSON_ArrayForEach(window, cJSON_GetObjectItemCaseSensitive(account, "windows")) {
        double reset; if (app_num(window, "reset_at", &reset) && (!when || reset < when)) when = reset;
    }
    return when;
}
static void apply(const void *vm, view_state_t state)
{
    const cJSON *summary_data = cJSON_GetObjectItemCaseSensitive(vm, "summary");
    app_show(summary, summary_data);
    if (summary_data) {
        double requests, failures;char value[32];const char *tokens = app_str(summary_data, "tok_today_txt");
        bool has_requests = app_num(summary_data, "req_today", &requests), has_failures = app_num(summary_data, "fail_pct", &failures);
        app_show(stat_cells[0], has_requests); if (has_requests) {snprintf(value,sizeof(value),"%.0f",requests);stat_set(stats[0],value);}
        app_show(stat_cells[1], tokens); if (tokens) stat_set(stats[1],tokens);
        app_show(stat_cells[2], has_failures); if (has_failures) {snprintf(value,sizeof(value),"%.1f%%",failures);stat_set(stats[2],value);}
    }
    const cJSON *account = ai_current(vm);
    app_show(root, account); app_show(empty, !account);
    if (!account) return;
    const cJSON *accounts = cJSON_GetObjectItemCaseSensitive(vm, "accounts");
    unsigned index = 0; while (cJSON_GetArrayItem(accounts, index) != account) ++index;
    ui_pager_set(account_pager, cJSON_GetArraySize(accounts), index);
    const char *kind = app_str(account, "kind"), *state_name = account_status(account);
    ui_icon_set(provider, provider_icon(kind), NUKA_COLOR_AVO);
    ui_text_set(label, "%s", app_str(account, "label"));
    char key[40]; snprintf(key, sizeof(key), "ai.status.%s", state_name);
    ui_chip_set(status, tr(key), status_color(state_name));
    const cJSON *five = find_window(account, "5h"), *weekly = find_window(account, "weekly");
    ring_apply(outer, five ? five : weekly); ring_apply(inner, five ? weekly : NULL);
    const cJSON *primary = five ? five : weekly;
    app_show(center_value, primary); app_show(center_caption, primary);
    if (primary) {
        animate_percentage((int32_t)((100 - cJSON_GetObjectItemCaseSensitive(primary, "used_pct")->valuedouble) * 10));
        if (five && weekly) ui_text_set(center_caption, "%s %.0f%%", window_label(weekly), 100 - cJSON_GetObjectItemCaseSensitive(weekly, "used_pct")->valuedouble);
        else ui_text_set(center_caption, "%s", window_label(primary));
    }
    bool recovering; double when = recovery_time(account, &recovering);
    app_show(reset_row, when);
    if (when) {
        char text[32]; app_countdown(text, sizeof(text), when);
        ui_icon_set(reset_icon, recovering ? ICON_MOON : ICON_RESET, recovering ? NUKA_COLOR_WARN : NUKA_COLOR_TEXT_DIM);
        ui_text_set(reset_text, "%s %s", tr(recovering ? "ai.recover" : "ai.reset"), text);
    }
}
static void refresh(lv_event_t *event)
{
    hub_send("ai.refresh", cJSON_CreateObject(), true);
    (void)event;
}
static void detail(lv_obj_t *page)
{
    cJSON *vm = vm_store_snapshot(VM_AI); const cJSON *account;
    cJSON_ArrayForEach(account, cJSON_GetObjectItemCaseSensitive(vm, "accounts")) {
        lv_obj_t *row = ui_box(page, LV_PCT(100), LV_SIZE_CONTENT);
        lv_obj_set_style_min_height(row, 100, 0); lv_obj_set_style_pad_ver(row, NUKA_SPACE_S, 0);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW); lv_obj_set_style_pad_column(row, NUKA_SPACE_M, 0);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_t *ring = ui_ring(row, 76, 7), *small = ui_ring(ring, 54, 5); lv_obj_center(small);
        const cJSON *five = find_window(account, "5h"), *weekly = find_window(account, "weekly");
        ring_apply(ring, five ? five : weekly); ring_apply(small, five ? weekly : NULL);
        lv_obj_t *body = ui_vstack(row, NUKA_SPACE_XS); lv_obj_set_width(body, 320);
        lv_obj_set_flex_align(body, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
        lv_obj_t *name = ui_hstack(body, NUKA_SPACE_S);
        ui_icon(name, provider_icon(app_str(account, "kind")), 22, NUKA_COLOR_AVO);
        lv_obj_t *text = ui_text(name, 22, NUKA_COLOR_TEXT); ui_text_set(text, "%s", app_str(account, "label"));
        ui_text_clip(text); lv_obj_set_style_max_width(text, 280, 0);
        char key[40]; const char *status_name = account_status(account); snprintf(key, sizeof(key), "ai.status.%s", status_name);
        text = ui_text(body, 22, status_color(status_name)); ui_text_set(text, "%s", tr(key));
        bool recovering; double when = recovery_time(account, &recovering);
        if (when) { char countdown[32]; app_countdown(countdown, sizeof(countdown), when); ui_text_set(ui_text(body, 22, NUKA_COLOR_TEXT_DIM), "%s %s", tr(recovering ? "ai.recover" : "ai.reset"), countdown); }
        const char *value = app_str(account, "credits_txt"); if (value) ui_text_set(ui_text(body, 22, NUKA_COLOR_TEXT_DIM), "%s %s", tr("ai.credits"), value);
        const cJSON *window;
        cJSON_ArrayForEach(window, cJSON_GetObjectItemCaseSensitive(account, "windows")) {
            char value_text[96], countdown[32]; double reset; int length = snprintf(value_text, sizeof(value_text), "%.0f%%", 100 - cJSON_GetObjectItemCaseSensitive(window, "used_pct")->valuedouble);
            if (app_num(window, "reset_at", &reset)) { app_countdown(countdown, sizeof(countdown), reset); snprintf(value_text + length, sizeof(value_text) - length, " · %s", countdown); }
            ui_list_row(page, window_label(window), value_text);
        }
        if ((value = app_str(account, "fresh_txt"))) ui_list_row(page, tr("ai.fresh"), value);
    }
    lv_obj_t *button = ui_button(page, tr("ai.refresh"), refresh, NULL); lv_group_add_obj(nav_focus_group(), button);
    lv_obj_update_layout(page);lv_obj_scroll_to_y(page,0,LV_ANIM_OFF);
    cJSON_Delete(vm);
}
const app_view_t ai_view = {create, apply, detail};
