// SPDX-License-Identifier: AGPL-3.0-or-later
#include "overlay.h"
#include "theme.h"
#include "widgets/widgets.h"
#include "nav.h"
#include "power.h"
#include "store.h"
#include "hub_client.h"
#include "usb.h"
#include "i18n.h"
#include "vault.h"
#include "battery.h"
#include "net.h"
#include "esp_log.h"
#include <stdio.h>
#include "esp_system.h"
#include <string.h>
#include <time.h>
static lv_obj_t *sheet, *toast, *toast_text, *hold_bar, *quick_tiles[2];
static overlay_kind_t active;
static cJSON *request;
static uint32_t opened, press_started;
static int focused;
static bool held_decision;
static bool reset_armed, reset_vault;
static cJSON *pending_permission;
static lv_obj_t *ota_cancel;
static const uint8_t priority[] = {255,1,2,3,4,5,6,6,4,4,4,6};
static void cancel(lv_event_t *e) { overlay_close(); }
static bool begin(overlay_kind_t kind, const char *title)
{
    if (active != OVERLAY_NONE && priority[active] < priority[kind]) return false;
    overlay_close(); active = kind; focused = 0; opened = lv_tick_get();
    lv_group_remove_all_objs(nav_focus_group());
    if (kind == OVERLAY_TOAST) { lv_label_set_text(toast_text, title); lv_obj_remove_flag(toast, LV_OBJ_FLAG_HIDDEN); return true; }
    lv_obj_remove_flag(sheet, LV_OBJ_FLAG_HIDDEN); lv_obj_scroll_to_y(sheet, 0, LV_ANIM_OFF);
    lv_obj_set_flex_align(sheet, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    if (title) { lv_obj_t *label = ui_text(sheet, 32, NUKA_COLOR_TEXT); lv_obj_set_width(label, LV_PCT(100)); lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0); lv_label_set_text(label, title); }
    return true;
}
void overlay_close(void)
{
    lv_obj_clean(sheet); lv_obj_add_flag(sheet, LV_OBJ_FLAG_HIDDEN); lv_obj_add_flag(toast, LV_OBJ_FLAG_HIDDEN);
    active = OVERLAY_NONE; hold_bar = NULL; cJSON_Delete(request); request = NULL;
    nav_restore_focus();
}
void overlay_init(void)
{
    // Modal content owns the whole screen, centred like an alert; toasts are a banner that leaves the card usable.
    sheet = ui_box(lv_screen_active(), 480, 480); lv_obj_add_flag(sheet, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_scroll_dir(sheet, LV_DIR_VER); lv_obj_set_style_bg_opa(sheet, LV_OPA_COVER, 0); lv_obj_set_style_bg_color(sheet, lv_color_hex(NUKA_COLOR_BG), 0);
    lv_obj_set_flex_flow(sheet, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_hor(sheet, NUKA_SPACE_XL, 0); lv_obj_set_style_pad_ver(sheet, 48, 0); lv_obj_set_style_pad_row(sheet, NUKA_SPACE_M, 0);
    toast = ui_hstack(lv_screen_active(), 0); lv_obj_add_flag(toast, LV_OBJ_FLAG_HIDDEN); lv_obj_align(toast, LV_ALIGN_TOP_MID, 0, NUKA_SPACE_S);
    lv_obj_set_style_bg_opa(toast, LV_OPA_COVER, 0); lv_obj_set_style_bg_color(toast, lv_color_hex(NUKA_COLOR_SURFACE_HI), 0);
    lv_obj_set_style_radius(toast, LV_RADIUS_CIRCLE, 0); lv_obj_set_style_pad_hor(toast, NUKA_SPACE_L, 0); lv_obj_set_style_pad_ver(toast, NUKA_SPACE_S, 0);
    toast_text = ui_text(toast, 28, NUKA_COLOR_TEXT); lv_obj_set_style_max_width(toast_text, 400, 0); lv_obj_set_style_text_align(toast_text, LV_TEXT_ALIGN_CENTER, 0);
}
static lv_obj_t *text(const char *value, int size, uint32_t color)
{
    lv_obj_t *label = ui_text(sheet, size, color); lv_obj_set_width(label, LV_PCT(100));
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0); lv_label_set_text(label, value);
    return label;
}
static lv_obj_t *add_button(lv_obj_t *parent, const char *label, bool primary, lv_event_cb_t cb, void *data)
{
    lv_obj_t *button = primary ? ui_button_primary(parent, label, cb, data) : ui_button(parent, label, cb, data);
    lv_obj_set_height(button, 72); if (parent != sheet) { lv_obj_set_flex_grow(button, 1); lv_obj_set_style_pad_hor(button, NUKA_SPACE_S, 0); }
    lv_group_add_obj(nav_focus_group(), button);
    return button;
}
static lv_obj_t *button_row(void)
{
    lv_obj_t *row = ui_hstack(sheet, NUKA_SPACE_M); lv_obj_set_width(row, LV_PCT(100)); lv_obj_set_style_pad_top(row, NUKA_SPACE_S, 0);
    return row;
}
// A round glyph button over a short caption, as in a control centre.
static lv_obj_t *tile(lv_obj_t *parent, ui_icon_t icon, const char *caption, lv_event_cb_t cb, void *data)
{
    lv_obj_t *column = ui_vstack(parent, NUKA_SPACE_S);
    lv_obj_t *button = ui_icon_button(column, icon, 88, false, cb, data);
    lv_obj_t *label = ui_text(column, 22, NUKA_COLOR_TEXT_DIM); lv_label_set_text(label, caption);
    lv_obj_set_width(label, 120); lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_group_add_obj(nav_focus_group(), button);
    return button;
}
static void tile_set(lv_obj_t *button, uint32_t background, uint32_t glyph)
{
    lv_obj_set_style_bg_color(button, lv_color_hex(background), 0);
    lv_obj_set_style_text_color(lv_obj_get_child(button, 0), lv_color_hex(glyph), 0);
}
static void tile_toggle(lv_obj_t *button, bool on) { tile_set(button, on ? NUKA_COLOR_AVO : NUKA_COLOR_SURFACE_HI, on ? NUKA_COLOR_BG : NUKA_COLOR_TEXT); }
static lv_obj_t *tile_row(void)
{
    lv_obj_t *row = ui_hstack(sheet, NUKA_SPACE_M); lv_obj_set_flex_align(row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    return row;
}
// Touch needs a visible way out of sheets that have no cancel action of their own.
static void close_button(void)
{
    lv_obj_t *button = ui_icon_button(sheet, ICON_CROSS, 56, false, cancel, NULL);
    lv_obj_add_flag(button, LV_OBJ_FLAG_IGNORE_LAYOUT); lv_obj_align(button, LV_ALIGN_TOP_LEFT, -NUKA_SPACE_S, -32);
}
static lv_obj_t *hold_meter(void)
{
    lv_obj_t *bar = lv_bar_create(sheet); lv_obj_set_size(bar, LV_PCT(100), 6); lv_bar_set_range(bar, 0, 100);
    lv_obj_set_style_bg_color(bar, lv_color_hex(NUKA_COLOR_SURFACE_HI), 0); lv_obj_set_style_bg_color(bar, lv_color_hex(NUKA_COLOR_AVO_SOFT), LV_PART_INDICATOR);
    return bar;
}
static void power_action(lv_event_t *e)
{
    intptr_t action = (intptr_t)lv_event_get_user_data(e);
    if (action == 0) power_shutdown("user");
    else if (action == 1) esp_restart();
    else overlay_close();
}
void overlay_power(void)
{
    if (!begin(OVERLAY_POWER, NULL)) return;
    lv_obj_t *row = tile_row();
    tile_set(tile(row, ICON_POWER, tr("power.shutdown"), power_action, (void *)0), NUKA_COLOR_DANGER, NUKA_COLOR_BG);
    tile(row, ICON_RESET, tr("power.restart"), power_action, (void *)1);
    tile(row, ICON_CROSS, tr("action.cancel"), power_action, (void *)2);
    lv_obj_set_style_pad_top(text(tr("power.hold"), 22, NUKA_COLOR_TEXT_DIM), NUKA_SPACE_L, 0);
}
static void quick_action(lv_event_t *e)
{
    intptr_t action = (intptr_t)lv_event_get_user_data(e);
    if (action == 0) { nuka_settings.dnd = !nuka_settings.dnd; tile_toggle(quick_tiles[0], nuka_settings.dnd); }
    if (action == 1 && battery_status().vbus_present) { nuka_settings.always_on = !nuka_settings.always_on; tile_toggle(quick_tiles[1], nuka_settings.always_on); }
    if (action == 2) { nav_show("settings"); overlay_close(); nav_detail(); }
    if (action == 3) { nav_show("pomodoro"); overlay_close(); nav_current()->on_action(ACT_PRIMARY); }
    if (action == 4) overlay_power();
    nav_runtime_error(store_save_settings());
}
static void brightness_changed(lv_event_t *e)
{
    nuka_settings.brightness = lv_slider_get_value(lv_event_get_target(e));
    nav_runtime_error(store_save_settings());power_reload_settings();power_activity();
}
void overlay_quick(void)
{
    if (!begin(OVERLAY_QUICK, NULL)) return;
    close_button();
    lv_obj_t *brightness = ui_hstack(sheet, NUKA_SPACE_M); lv_obj_set_style_pad_bottom(brightness, NUKA_SPACE_M, 0);
    ui_icon(brightness, ICON_BRIGHT, 28, NUKA_COLOR_TEXT_DIM);
    lv_obj_t *slider = lv_slider_create(brightness); lv_obj_set_size(slider, 300, 32);
    lv_slider_set_range(slider, 0, 100); lv_slider_set_value(slider, nuka_settings.brightness, LV_ANIM_OFF);
    lv_obj_add_event_cb(slider, brightness_changed, LV_EVENT_VALUE_CHANGED, NULL);
    lv_group_add_obj(nav_focus_group(), slider);
    lv_obj_t *row = tile_row();
    quick_tiles[0] = tile(row, ICON_MOON, tr("settings.dnd"), quick_action, (void *)0); tile_toggle(quick_tiles[0], nuka_settings.dnd);
    quick_tiles[1] = tile(row, ICON_BRIGHT, tr("settings.always_on"), quick_action, (void *)1); tile_toggle(quick_tiles[1], nuka_settings.always_on);
    lv_obj_set_state(quick_tiles[1], LV_STATE_DISABLED, !battery_status().vbus_present);
    tile(row, ICON_TIMER, tr("app.pomodoro"), quick_action, (void *)3);
    row = tile_row();
    tile(row, ICON_WIFI, tr("settings.wifi"), quick_action, (void *)2);
    tile(row, ICON_POWER, tr("power.menu"), quick_action, (void *)4);
}
void overlay_provisioning(void)
{
    if(!begin(OVERLAY_QUICK,tr("pair.ap")))return;
    close_button();
    lv_obj_t *row=ui_hstack(sheet,NUKA_SPACE_L);
    // A light quiet zone around the code keeps it scannable on the black screen.
    lv_obj_t *qr=lv_qrcode_create(row);lv_qrcode_set_size(qr,168);
    lv_qrcode_set_dark_color(qr,lv_color_black());lv_qrcode_set_light_color(qr,lv_color_white());
    lv_obj_set_style_border_width(qr,8,0);lv_obj_set_style_border_color(qr,lv_color_white(),0);
    if(lv_qrcode_update(qr,net_provision_url(),strlen(net_provision_url()))!=LV_RESULT_OK)nav_runtime_error(ESP_ERR_INVALID_SIZE);
    lv_obj_t *info=ui_vstack(row,NUKA_SPACE_XS);lv_obj_set_flex_align(info,LV_FLEX_ALIGN_CENTER,LV_FLEX_ALIGN_START,LV_FLEX_ALIGN_START);
    lv_obj_t *ssid=ui_text(info,28,NUKA_COLOR_TEXT);lv_obj_set_width(ssid,180);ui_text_clip(ssid);lv_label_set_text(ssid,net_provision_ssid());
    lv_obj_t *code=ui_text(info,40,NUKA_COLOR_AVO);lv_label_set_text(code,net_pair_code());
    text(tr("pair.open_page"),22,NUKA_COLOR_TEXT_DIM);text(net_provision_url(),22,NUKA_COLOR_AVO);
}
static void dismiss(lv_event_t *e)
{
    const char *nid = lv_event_get_user_data(e);
    cJSON *d = cJSON_CreateObject();
    if (nid) cJSON_AddStringToObject(d, "nid", nid); else cJSON_AddBoolToObject(d, "all", true);
    hub_send("notify.dismiss", d, false); overlay_notifications();
}
static void reset_now(void)
{
    esp_err_t error=store_factory_reset(reset_vault);nav_runtime_error(error);if(error==ESP_OK)esp_restart();
}
static void reset_touch(lv_event_t *event)
{
    lv_event_code_t code=lv_event_get_code(event);
    if(code==LV_EVENT_PRESSED){press_started=lv_tick_get();held_decision=false;}
    if(code==LV_EVENT_PRESSING&&!held_decision){lv_bar_set_value(hold_bar,(lv_tick_get()-press_started)/30,LV_ANIM_OFF);if(lv_tick_get()-press_started>=3000){held_decision=true;reset_now();}}
    if(code==LV_EVENT_RELEASED||code==LV_EVENT_PRESS_LOST)lv_bar_set_value(hold_bar,0,LV_ANIM_OFF);
}
static void toggle_reset_vault(lv_event_t *event)
{
    reset_vault=!reset_vault;lv_obj_t *button=lv_event_get_target(event);
    lv_obj_set_style_bg_color(button,lv_color_hex(reset_vault?NUKA_COLOR_DANGER:NUKA_COLOR_SURFACE_HI),0);
    lv_obj_set_style_text_color(lv_obj_get_child(button,0),lv_color_hex(reset_vault?NUKA_COLOR_BG:NUKA_COLOR_TEXT),0);
}
void overlay_notifications(void)
{
    if (!begin(OVERLAY_NOTIFY, tr("notify.title"))) return;
    close_button();
    request = vm_store_snapshot(VM_NOTIFY);
    const cJSON *items = cJSON_GetObjectItemCaseSensitive(request, "items"), *item;
    if (!cJSON_GetArraySize(items)) {
        ui_icon(sheet, ICON_BELL, 64, NUKA_COLOR_AVO_DEEP); text(tr("notify.none"), 28, NUKA_COLOR_TEXT_DIM);
        return;
    }
    lv_obj_set_flex_align(sheet, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    // Each row dismisses itself on tap, like clearing a single banner.
    cJSON_ArrayForEach(item, items) {
        const char *nid = cJSON_GetObjectItemCaseSensitive(item, "nid")->valuestring, *level = cJSON_GetObjectItemCaseSensitive(item, "level")->valuestring;
        lv_obj_t *row = ui_row(sheet, 72, dismiss, (void *)nid); lv_group_add_obj(nav_focus_group(), row);
        ui_icon(row, ICON_DOT, 16, !strcmp(level, "critical") ? NUKA_COLOR_DANGER : !strcmp(level, "warn") ? NUKA_COLOR_WARN : NUKA_COLOR_AVO);
        lv_obj_t *title = ui_text(row, 28, NUKA_COLOR_TEXT); lv_obj_set_flex_grow(title, 1); ui_text_clip(title);
        lv_label_set_text(title, cJSON_GetObjectItemCaseSensitive(item, "title")->valuestring);
    }
    lv_obj_set_width(add_button(sheet, tr("notify.clear"), false, dismiss, NULL), LV_PCT(100));
}
static void recovery_action(lv_event_t *e)
{
    intptr_t action = (intptr_t)lv_event_get_user_data(e);
    if (action == 0) { usb_request_provisioning(); overlay_close(); }
    if (action == 1) { bool entered = usb_enter_admin(); overlay_close(); if (!entered) overlay_toast(tr("recovery.unlock")); }
    if (action == 2) usb_restart_download();
    if (action == 3) { reset_armed = true; lv_obj_clean(sheet); lv_group_remove_all_objs(nav_focus_group());
        ui_icon(sheet, ICON_ALERT, 64, NUKA_COLOR_DANGER); text(tr("settings.reset_confirm"), 28, NUKA_COLOR_TEXT);
        lv_obj_set_width(add_button(sheet,tr("settings.reset_vault"),false,toggle_reset_vault,NULL),LV_PCT(100));
        lv_obj_t *button=add_button(sheet,tr("recovery.confirm"),false,reset_touch,NULL);lv_obj_set_width(button,LV_PCT(100));
        lv_obj_set_style_text_color(lv_obj_get_child(button,0),lv_color_hex(NUKA_COLOR_DANGER),0);
        lv_obj_remove_event_cb(button,reset_touch);lv_obj_add_event_cb(button,reset_touch,LV_EVENT_ALL,NULL);
        hold_bar=hold_meter();
    }
}
void overlay_recovery(void)
{
    if (active == OVERLAY_RECOVERY && reset_armed) { reset_now();return; }
    if (!begin(OVERLAY_RECOVERY, tr("recovery.title"))) return;
    reset_armed = false; reset_vault = false;
    const char *keys[] = {"settings.wifi", "vault.manage", "recovery.download", "settings.factory"};
    for (intptr_t i = 0; i < 4; ++i) lv_obj_set_width(add_button(sheet, tr(keys[i]), false, recovery_action, (void *)i), LV_PCT(100));
}
void overlay_toast(const char *text)
{
    if (!begin(OVERLAY_TOAST, text)) return;
}
static void decide(const char *behavior)
{
    cJSON *d = cJSON_CreateObject();
    cJSON_AddStringToObject(d, "rid", cJSON_GetObjectItemCaseSensitive(request, "rid")->valuestring);
    cJSON_AddStringToObject(d, "behavior", behavior); hub_send("perm.decide", d, true);
    overlay_close(); power_set_permission_pending(false);
}
static void permission_touch(lv_event_t *event)
{
    lv_event_code_t code = lv_event_get_code(event);
    if (code == LV_EVENT_PRESSED) { press_started = lv_tick_get(); held_decision = false; }
    if (code == LV_EVENT_PRESSING && !held_decision) lv_bar_set_value(hold_bar,(lv_tick_get()-press_started)/5,LV_ANIM_OFF);
    if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) lv_bar_set_value(hold_bar,0,LV_ANIM_OFF);
    if (code == LV_EVENT_PRESSING && !held_decision && lv_tick_get() - press_started >= 500) {
        held_decision = true; decide(lv_event_get_user_data(event));
    }
}
void overlay_permission(const cJSON *r)
{
    if (r != pending_permission) { cJSON_Delete(pending_permission); pending_permission = cJSON_Duplicate(r, true); }
    if (!begin(OVERLAY_PERMISSION, NULL)) return;
    request = cJSON_Duplicate(r, true); power_activity(); power_set_permission_pending(true);
    // The tool is the question being asked, so it leads; project and summary give it context.
    const char *fields[] = {"tool", "project", "summary"};
    const int sizes[] = {40, 28, 22};
    const uint32_t colors[] = {NUKA_COLOR_WARN, NUKA_COLOR_TEXT, NUKA_COLOR_TEXT_DIM};
    for (size_t i = 0; i < 3; ++i) {
        cJSON *value = cJSON_GetObjectItemCaseSensitive(r, fields[i]);
        if (value) text(value->valuestring, sizes[i], colors[i]);
    }
    const char *behaviors[] = {"deny", "allow"};
    const char *labels[] = {tr("permission.hold_deny"), tr("permission.hold_allow")};
    lv_obj_t *row = button_row();
    for (int i = 0; i < 2; ++i) {
        lv_obj_t *button = add_button(row, labels[i], i == 1, cancel, NULL);
        lv_obj_remove_event_cb(button, cancel); lv_obj_set_height(button, 88); lv_group_remove_obj(button);
        lv_obj_add_event_cb(button, permission_touch, LV_EVENT_ALL, (void *)behaviors[i]);
    }
    hold_bar = hold_meter();
}
void overlay_permission_done(const cJSON *d)
{
    const char *rid = cJSON_GetObjectItemCaseSensitive(d, "rid")->valuestring;
    if (pending_permission && !strcmp(cJSON_GetObjectItemCaseSensitive(pending_permission, "rid")->valuestring, rid)) { cJSON_Delete(pending_permission); pending_permission = NULL; power_set_permission_pending(false); }
    if (active == OVERLAY_PERMISSION && strcmp(cJSON_GetObjectItemCaseSensitive(request, "rid")->valuestring,
        cJSON_GetObjectItemCaseSensitive(d, "rid")->valuestring) == 0) {
        overlay_close();
        char text[160],by[48],result[48];snprintf(by,sizeof(by),"permission.by.%s",cJSON_GetObjectItemCaseSensitive(d,"by")->valuestring);snprintf(result,sizeof(result),"permission.result.%s",cJSON_GetObjectItemCaseSensitive(d,"result")->valuestring);
        snprintf(text,sizeof(text),"%s · %s · %s",tr("permission.done"),tr(by),tr(result));overlay_toast(text);
    }
}
void overlay_notification(const cJSON *notification)
{
    const char *level = cJSON_GetObjectItemCaseSensitive(notification, "level")->valuestring;
    if (nuka_settings.dnd) return;
    if (strcmp(level, "critical") == 0) {
        if (!begin(OVERLAY_CRITICAL, cJSON_GetObjectItemCaseSensitive(notification, "title")->valuestring)) return;
        power_activity();
        lv_obj_move_to_index(ui_icon(sheet, ICON_ALERT, 72, NUKA_COLOR_DANGER), 0);
        cJSON *body = cJSON_GetObjectItemCaseSensitive(notification, "body");
        if (body) text(body->valuestring, 28, NUKA_COLOR_TEXT_DIM);
        lv_obj_set_width(add_button(sheet, tr("common.close"), true, cancel, NULL), LV_PCT(100));
    } else overlay_toast(cJSON_GetObjectItemCaseSensitive(notification, "title")->valuestring);
}
static void confirm_pair(lv_event_t *e)
{
    bool accept = (intptr_t)lv_event_get_user_data(e);
    usb_pair_decide(accept); overlay_close();
}
void overlay_pair(const cJSON *data)
{
    if (!begin(OVERLAY_PAIR, tr("pair.title"))) return;
    text(cJSON_GetObjectItemCaseSensitive(data, "hub_name")->valuestring, 28, NUKA_COLOR_TEXT_DIM);
    text(cJSON_GetObjectItemCaseSensitive(data, "code")->valuestring, 56, NUKA_COLOR_AVO);
    lv_obj_t *row = button_row();
    add_button(row, tr("pair.reject"), false, confirm_pair, (void *)false);
    lv_obj_t *confirm = add_button(row, tr("action.confirm"), true, confirm_pair, (void *)true);
    lv_group_focus_obj(confirm);
}
static void confirm_vault(lv_event_t *event) { usb_vault_decide((intptr_t)lv_event_get_user_data(event)); overlay_close(); }
void overlay_vault(const cJSON *data)
{
    const char *action = cJSON_GetObjectItemCaseSensitive(data, "action")->valuestring;
    if (!begin(OVERLAY_VAULT, tr(!strcmp(action, "export") ? "vault.export" : "vault.import"))) return;
    const cJSON *count = cJSON_GetObjectItemCaseSensitive(data, "count"), *added = cJSON_GetObjectItemCaseSensitive(data, "added"), *skipped = cJSON_GetObjectItemCaseSensitive(data, "skipped");
    char line[100];
    if (count) { snprintf(line, sizeof(line), "%s %d", tr("metric.total"), count->valueint); text(line, 28, NUKA_COLOR_TEXT_DIM); }
    if (added && skipped) { snprintf(line, sizeof(line), "%s %d / %s %d", tr("vault.added"), added->valueint, tr("vault.skipped"), skipped->valueint); text(line, 28, NUKA_COLOR_TEXT_DIM); }
    lv_obj_t *row = button_row();
    add_button(row, tr("action.cancel"), false, confirm_vault, (void *)false); add_button(row, tr("action.confirm"), true, confirm_vault, (void *)true);
}
void overlay_usb_closed(bool vault) { if (active == (vault ? OVERLAY_VAULT : OVERLAY_PAIR)) overlay_close(); }
void overlay_ota(const cJSON *offer,lv_event_cb_t callback)
{
    if(!begin(OVERLAY_OTA,tr("ota.available")))return;
    text(cJSON_GetObjectItemCaseSensitive(offer,"ver")->valuestring,40,NUKA_COLOR_AVO);
    const cJSON *notes=cJSON_GetObjectItemCaseSensitive(offer,"notes_txt");if(notes)text(notes->valuestring,22,NUKA_COLOR_TEXT_DIM);
    lv_obj_t *row=button_row();
    ota_cancel=add_button(row,tr("action.cancel"),false,callback,(void *)false);add_button(row,tr("ota.install"),true,callback,(void *)true);
}
bool overlay_permission_pending(void) { return pending_permission != NULL; }
void overlay_permission_reopen(void) { if (pending_permission) overlay_permission(pending_permission); }
bool overlay_key(const key_event_t *key)
{
    if (active == OVERLAY_NONE) return false;
    if (active == OVERLAY_PERMISSION) {
        if (key->kind == KEYEV_LONG) decide(key->key == KEY_BOOT ? "allow" : "deny");
        if (key->kind == KEYEV_HOLD) lv_bar_set_value(hold_bar, key->held_ms / 10, LV_ANIM_OFF);
        if (key->kind == KEYEV_RELEASE) lv_bar_set_value(hold_bar, 0, LV_ANIM_OFF);
        return true;
    }
    if (active == OVERLAY_TOAST) {
        if (key->key == KEY_KEY3 && key->kind == KEYEV_SHORT) { overlay_close(); return true; }
        return false;
    }
    if (key->key == KEY_KEY3 && key->kind == KEYEV_SHORT) {
        if (active == OVERLAY_OTA) { lv_obj_send_event(ota_cancel, LV_EVENT_CLICKED, NULL); return true; }
        if (active == OVERLAY_PAIR) usb_pair_decide(false);
        if (active == OVERLAY_VAULT) usb_vault_decide(false);
        overlay_close(); return true;
    }
    if (key->key == KEY_BOOT && key->kind == KEYEV_SHORT) lv_group_focus_next(nav_focus_group());
    if (key->key == KEY_BOOT && key->kind == KEYEV_LONG) {
        lv_obj_t *object = lv_group_get_focused(nav_focus_group());
        if (object) lv_obj_send_event(object, LV_EVENT_CLICKED, NULL);
    }
    return true;
}
void overlay_tick(void)
{
    if (active == OVERLAY_TOAST && lv_tick_get() - opened >= 3000) overlay_close();
    if (pending_permission && time(NULL) >=
        cJSON_GetObjectItemCaseSensitive(pending_permission, "expires_at")->valuedouble) {
        cJSON_Delete(pending_permission);pending_permission=NULL;
        if(active==OVERLAY_PERMISSION)overlay_close();
        power_set_permission_pending(false);
    }
}
overlay_kind_t overlay_current(void) { return active; }
