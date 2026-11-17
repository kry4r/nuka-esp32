// SPDX-License-Identifier: AGPL-3.0-or-later
#include "keynav.h"
#include "nav.h"
#include "overlay.h"
#include "store.h"
#include "power.h"
#include "macro.h"
#include "vault.h"
#include "vault_app.h"
#include "battery.h"
#include "theme.h"
#include "home_app.h"
#include "hub_client.h"
#include "nuka_events.h"
#include "i18n.h"
#include <string.h>
static uint32_t pending_key;
static bool key_down;
static lv_obj_t *hold_ring;
static bool talk_pressed, talk_started, talk_ready;
static bool interrupted_reply,stop_short;
static bool talk_allowed(void)
{
    overlay_kind_t overlay=overlay_current();
    return !strcmp(nuka_settings.key3_action,"talk")&&strcmp(nav_current()->id,"vault")&&
        (overlay==OVERLAY_NONE||overlay==OVERLAY_TOAST);
}
static void talk_cancel(void)
{
    if(talk_ready)nuka_post(NUKA_VOICE_CANCEL,NULL,0);
    talk_pressed=talk_started=talk_ready=false;
    interrupted_reply=false;
    home_voice_set(HOME_VOICE_IDLE,"",0);
}
void keynav_tick(void)
{
    if(talk_pressed&&!talk_allowed())talk_cancel();
}
static void talk_start(void)
{
    talk_started=true;nav_home();
    if(talk_ready){nuka_post(NUKA_VOICE_START,NULL,0);home_voice_set(HOME_VOICE_LISTENING,"",0);}
    else {const char *reason=tr(hub_connected()?"voice.disabled":"voice.offline");home_voice_set(HOME_VOICE_ERROR,reason,0);overlay_toast(reason);}
}
static bool talk_key(const key_event_t *event)
{
    if(event->key==KEY_KEY3&&stop_short&&(event->kind==KEYEV_SHORT||event->kind==KEYEV_DOUBLE)) {stop_short=false;return true;}
    if(event->kind==KEYEV_COMBO_HOLD||(event->key==KEY_BOOT&&event->kind==KEYEV_PRESS)) {
        if(talk_pressed)talk_cancel();
        return false;
    }
    if(event->key!=KEY_KEY3)return false;
    if(event->kind==KEYEV_RELEASE&&talk_pressed) {
        stop_short=interrupted_reply&&event->held_ms<400;interrupted_reply=false;
        if(!talk_started&&event->held_ms>=400)talk_start();
        if(talk_ready)nuka_post(talk_started?NUKA_VOICE_END:NUKA_VOICE_CANCEL,NULL,0);
        if(home_voice_state()!=HOME_VOICE_ERROR)home_voice_set(HOME_VOICE_IDLE,"",0);
        talk_pressed=talk_started=talk_ready=false;return true;
    }
    if(!talk_allowed())return false;
    if(event->kind==KEYEV_PRESS) {
        interrupted_reply=home_voice_state()==HOME_VOICE_SPEAKING||home_voice_state()==HOME_VOICE_THINKING;
        if(interrupted_reply){nuka_post(NUKA_VOICE_STOP,NULL,0);home_voice_set(HOME_VOICE_IDLE,"",0);}
        talk_pressed=true;talk_started=false;talk_ready=nuka_settings.assistant&&hub_connected();
        if(talk_ready)nuka_post(NUKA_VOICE_PREPARE,NULL,0);
        return true;
    }
    if(event->kind==KEYEV_HOLD&&talk_pressed) {
        if(event->held_ms>=400&&!talk_started)talk_start();
        if(event->held_ms>=30000) {
            if(talk_ready)nuka_post(NUKA_VOICE_END,NULL,0);
            if(home_voice_state()!=HOME_VOICE_ERROR)home_voice_set(HOME_VOICE_IDLE,"",0);
            talk_pressed=talk_started=talk_ready=false;
        }
        return true;
    }
    if(event->kind==KEYEV_LONG)return true;
    if(event->kind==KEYEV_SHORT&&(home_voice_state()==HOME_VOICE_SPEAKING||home_voice_state()==HOME_VOICE_THINKING)) {
        nuka_post(NUKA_VOICE_STOP,NULL,0);home_voice_set(HOME_VOICE_IDLE,"",0);return true;
    }
    return false;
}
// SPEC §6.9.1: holds show progress along the screen edge. A short grace period keeps quick presses from flashing it,
// and it leaves as soon as the hold has done its job so it never sits over the result.
static void hold_feedback(const key_event_t *event)
{
    enum { GRACE = 250 };
    if (event->kind == KEYEV_RELEASE) { lv_obj_add_flag(hold_ring, LV_OBJ_FLAG_HIDDEN); return; }
    if (event->kind != KEYEV_HOLD && event->kind != KEYEV_COMBO_HOLD) return;
    unsigned limit = event->kind == KEYEV_COMBO_HOLD ? 3000 : event->key == KEY_PWR ? 4000 : event->key==KEY_KEY3&&talk_allowed()?400:1000;
    bool visible = event->held_ms >= GRACE && event->held_ms < limit && overlay_current() != OVERLAY_PERMISSION;
    lv_obj_set_flag(hold_ring, LV_OBJ_FLAG_HIDDEN, !visible);
    if (visible) lv_arc_set_value(hold_ring, (event->held_ms - GRACE) * 1000 / (limit - GRACE));
}
static void keypad_read(lv_indev_t *input, lv_indev_data_t *data)
{
    data->key = pending_key;
    data->state = key_down ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
    key_down = false;
}
void keynav_init(void)
{
    lv_indev_t *keypad = lv_indev_create();
    lv_indev_set_type(keypad, LV_INDEV_TYPE_KEYPAD);
    lv_indev_set_group(keypad, nav_focus_group());
    lv_indev_set_read_cb(keypad, keypad_read);
    hold_ring = ui_ring(lv_layer_top(), 474, 6); lv_obj_center(hold_ring);
    lv_obj_set_style_arc_opa(hold_ring, LV_OPA_TRANSP, 0); lv_obj_set_style_arc_color(hold_ring, lv_color_hex(NUKA_COLOR_AVO_SOFT), LV_PART_INDICATOR);
    lv_obj_add_flag(hold_ring, LV_OBJ_FLAG_HIDDEN);
}
void keynav_handle(const key_event_t *event)
{
    hold_feedback(event);
    if (event->kind == KEYEV_POWER_MENU) { overlay_power(); return; }
    if (event->kind == KEYEV_POWER_OFF) { power_shutdown("user"); return; }
    if (event->kind == KEYEV_RECOVERY) { overlay_recovery(); return; }
    if (event->key == KEY_PWR) {
        if (event->kind == KEYEV_SHORT) power_screen_off();
        return;
    }
    keynav_tick();
    if(talk_key(event))return;
    if (overlay_key(event)) return;
    // Vault keeps its original short-press interval while the assistant uses 400ms elsewhere.
    if(event->key==KEY_KEY3&&event->kind==KEYEV_RELEASE&&event->held_ms>=400&&event->held_ms<600&&!talk_allowed()){
        key_event_t short_press=*event;short_press.kind=KEYEV_SHORT;keynav_handle(&short_press);return;
    }
    const app_def_t *app = nav_current();
    if (strcmp(app->id, "vault") == 0 && !nav_arranging()) {
        if (event->key == KEY_KEY3 && event->kind == KEYEV_DOUBLE) { vault_lock(); return; }
        if (!vault_unlocked() && nav_in_detail()) {
            if (event->key == KEY_KEY3 && event->kind == KEYEV_SHORT) vault_app_command(VUI_CANCEL,0);
            return;
        }
        if (vault_unlocked() && event->key == KEY_BOOT) {
            if (event->kind == KEYEV_SHORT) vault_app_next();
            if (event->kind == KEYEV_LONG) vault_app_primary();
            return;
        }
        if(vault_unlocked()&&event->key==KEY_KEY3&&event->kind==KEYEV_LONG)return;
    }
    if (nav_in_detail()) {
        if (event->key == KEY_BOOT) {
            if (event->kind == KEYEV_SHORT) { pending_key = LV_KEY_NEXT; key_down = true; }
            if (event->kind == KEYEV_LONG) { pending_key = LV_KEY_ENTER; key_down = true; }
        } else {
            if (event->kind == KEYEV_SHORT) nav_back();
            if (event->kind == KEYEV_LONG) nav_home();
        }
        return;
    }
    if (event->key == KEY_BOOT && event->kind == KEYEV_SHORT) nav_next();
    if (event->key == KEY_BOOT && event->kind == KEYEV_LONG) app->on_action(ACT_PRIMARY);
    if (event->key == KEY_KEY3 && event->kind == KEYEV_SHORT) {
        if (strcmp(app->id, "clock") == 0) home_toggle_face();
        else nav_home();
    }
    if (event->key == KEY_KEY3 && event->kind == KEYEV_DOUBLE) nav_show(nuka_settings.key3_double_card);
    if (event->key == KEY_KEY3 && event->kind == KEYEV_LONG) {
        if (strcmp(app->id, "pomodoro") == 0) app->on_action(ACT_SECONDARY);
        else if (strcmp(nuka_settings.key3_action, "dnd") == 0) nuka_settings.dnd = !nuka_settings.dnd;
        else if (strcmp(nuka_settings.key3_action, "always_on") == 0 && battery_status().vbus_present) nuka_settings.always_on = !nuka_settings.always_on;
        else if (strcmp(nuka_settings.key3_action, "pomodoro") == 0) {
            nav_show("pomodoro"); nav_current()->on_action(ACT_PRIMARY);
        } else if (strcmp(nuka_settings.key3_action, "macro") == 0) nav_runtime_error(macro_execute(nuka_settings.key3_macro));
    }
}
