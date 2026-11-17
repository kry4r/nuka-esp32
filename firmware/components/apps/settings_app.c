// SPDX-License-Identifier: AGPL-3.0-or-later
#include "settings_app.h"
#include "store.h"
#include "i18n.h"
#include "net.h"
#include "hub_client.h"
#include "usb.h"
#include "vault.h"
#include "nav.h"
#include "overlay.h"
#include "nuka_events.h"
#include "perf_hud.h"
#include "esp_log.h"
#include <string.h>
extern const app_view_t settings_view;
void settings_apply(unsigned setting,int value)
{
    esp_err_t err=ESP_OK;
    if(setting==0){strcpy(nuka_settings.lang,value?"en":"zh-CN");nuka_settings.local_overrides|=1;i18n_set_language(nuka_settings.lang);}
    if(setting==1)nuka_settings.brightness=value;
    if(setting==2){nuka_settings.t_dim=value;nuka_settings.local_overrides|=2;}
    if(setting==3){nuka_settings.t_off=value;nuka_settings.local_overrides|=4;}
    if(setting==4)nuka_settings.raise=value;
    if(setting==5)nuka_settings.flip_action=value;
    if(setting==6)nuka_settings.charge_mode=value;
    if(setting==7)nuka_settings.charge_ma_index=value;
    if(setting==8){strcpy(nuka_settings.key3_action,(const char *[]){"talk","dnd","always_on","pomodoro","macro","none"}[value]);nuka_settings.local_overrides|=8;}
    if(setting==9)nuka_settings.key3_macro=value;
    if(setting==10){strcpy(nuka_settings.key3_double_card,nuka_apps[value]->id);nuka_settings.local_overrides|=16;}
    if(setting==11){nuka_settings.clock_style=value;nuka_settings.local_overrides|=32;}
    if(setting==12){if(vault_unlocked())err=vault_set_policy((uint8_t[]){10,20,0}[value],vault_status().unlock_seconds);else overlay_toast(tr("recovery.unlock"));}
    if(setting==13){if(vault_unlocked())err=vault_set_policy(vault_status().wipe_after,(uint16_t[]){60,120,300,600}[value]);else overlay_toast(tr("recovery.unlock"));}
    if(setting==14)usb_request_provisioning();
    if(setting==15)overlay_recovery();
    if(setting==16)nav_arrange();
    if(setting==17)perf_hud_show(value);
    if(setting==18){if(hub_connected()){hub_send("ota.check",cJSON_CreateObject(),true);overlay_toast(tr("state.loading"));}else overlay_toast(tr("connection.disconnected"));}
    if(setting<12&&err==ESP_OK)err=store_save_settings();
    nav_runtime_error(err);
    nuka_post(NUKA_CONFIG_CHANGED,NULL,0);
}
static void action(app_action_t a){if(a==ACT_PRIMARY)nav_detail();}
const app_def_t settings_app={"settings",VM_NONE,&settings_view,action};
