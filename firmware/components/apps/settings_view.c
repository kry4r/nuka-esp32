// SPDX-License-Identifier: AGPL-3.0-or-later
#include "settings_app.h"
#include "app_helpers.h"
#include "store.h"
#include "net.h"
#include "hub_client.h"
#include "vault.h"
#include "esp_app_desc.h"
#include "esp_heap_caps.h"
#include "nukapack.h"
#include "esp_littlefs.h"
#include "battery.h"
#include "nvs.h"
#include <stdio.h>
#include <string.h>
static lv_obj_t *wifi_chip, *hub_chip;
static void create(lv_obj_t *card)
{
    lv_obj_t *root = app_root(card);
    ui_icon(root, ICON_GEAR, 96, NUKA_COLOR_AVO);
    lv_obj_t *chips = ui_vstack(root, NUKA_SPACE_S); lv_obj_set_style_pad_top(chips, NUKA_SPACE_S, 0);
    wifi_chip = ui_chip(chips); hub_chip = ui_chip(chips);
    lv_obj_t *version = ui_text(root, 22, NUKA_COLOR_TEXT_DIM); ui_text_set(version, "nuka-esp32 %s", esp_app_get_description()->version);
}
static void apply(const void *vm, view_state_t state)
{
    char text[96];
    bool wifi = net_connected(), hub = hub_connected();
    snprintf(text, sizeof(text), "%s · %s", tr("settings.wifi"), tr(wifi ? "connection.connected" : "connection.disconnected"));
    ui_chip_set(wifi_chip, text, wifi ? NUKA_COLOR_AVO : NUKA_COLOR_DANGER);
    snprintf(text, sizeof(text), "Hub · %s", tr(hub ? "connection.connected" : "connection.connecting"));
    ui_chip_set(hub_chip, text, hub ? NUKA_COLOR_AVO : NUKA_COLOR_WARN);
}
static void changed(lv_event_t *e)
{
    unsigned setting=(uintptr_t)lv_event_get_user_data(e);lv_obj_t *obj=lv_event_get_target(e);
    int value=lv_obj_check_type(obj,&lv_slider_class)?lv_slider_get_value(obj):lv_dropdown_get_selected(obj);
    settings_apply(setting,value);
}
static lv_obj_t *setting_row(lv_obj_t *page, const char *label)
{
    lv_obj_t *row = ui_box(page, LV_PCT(100), 72);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW); lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row, NUKA_SPACE_S, 0);
    lv_obj_t *text = ui_text(row, 28, NUKA_COLOR_TEXT); lv_label_set_text(text, label);
    ui_text_clip(text); lv_obj_set_flex_grow(text, 1);
    return row;
}
static void dropdown(lv_obj_t *page,const char *label,const char *options,unsigned selected,unsigned setting)
{
    lv_obj_t *obj=lv_dropdown_create(setting_row(page,label));lv_obj_set_size(obj,200,56);lv_dropdown_set_options(obj,options);lv_dropdown_set_selected(obj,selected);
    // The font has no LV_SYMBOL glyphs; a trailing chevron marks the field as a menu.
    lv_dropdown_set_symbol(obj,NULL);lv_obj_set_style_text_color(obj,lv_color_hex(NUKA_COLOR_TEXT_DIM),0);
    lv_obj_set_style_pad_right(obj,44,0);
    lv_obj_t *chevron=ui_icon(obj,ICON_CHEVRON,20,NUKA_COLOR_TEXT_DIM);lv_obj_align(chevron,LV_ALIGN_RIGHT_MID,28,0);
    lv_obj_add_event_cb(obj,changed,LV_EVENT_VALUE_CHANGED,(void *)(uintptr_t)setting);lv_group_add_obj(nav_focus_group(),obj);
}
static void clicked(lv_event_t *e){settings_apply((uintptr_t)lv_event_get_user_data(e),0);}
static void action_row(lv_obj_t *page,const char *label,unsigned setting,uint32_t color)
{
    lv_obj_t *row=ui_row(page,72,clicked,(void *)(uintptr_t)setting);lv_obj_set_style_pad_hor(row,0,0);
    lv_obj_t *text=ui_text(row,28,color);lv_label_set_text(text,label);lv_obj_set_flex_grow(text,1);
    ui_icon(row,ICON_CHEVRON,22,NUKA_COLOR_TEXT_DIM);
    lv_group_add_obj(nav_focus_group(),row);
}
// Timeouts step through presets; a value set elsewhere (Hub config) steps to its neighbouring preset.
static const uint16_t presets[] = {5, 10, 15, 20, 30, 45, 60, 120, 300, 600};
static void show_seconds(lv_obj_t *label, unsigned seconds)
{
    if (seconds >= 60 && seconds % 60 == 0) ui_text_set(label, "%u min", seconds / 60); else ui_text_set(label, "%u s", seconds);
}
// Row children are label, minus, value, plus.
static void timeout_step(lv_event_t *event)
{
    lv_obj_t *button = lv_event_get_target(event), *value = lv_obj_get_child(lv_obj_get_parent(button), 2);
    unsigned seconds = (uintptr_t)lv_obj_get_user_data(value), count = sizeof(presets) / sizeof(presets[0]), next = seconds;
    if (lv_obj_get_index(button) == 3) { for (unsigned i = 0; i < count && next == seconds; ++i) if (presets[i] > seconds) next = presets[i]; }
    else for (unsigned i = count; i-- > 0 && next == seconds;) if (presets[i] < seconds) next = presets[i];
    if (next == seconds) return;
    lv_obj_set_user_data(value, (void *)(uintptr_t)next); show_seconds(value, next);
    settings_apply((uintptr_t)lv_event_get_user_data(event), next);
}
static void timeout_control(lv_obj_t *page, const char *label, uint32_t seconds, unsigned setting)
{
    lv_obj_t *row = setting_row(page, label);
    lv_obj_t *minus = ui_icon_button(row, ICON_MINUS, 56, false, timeout_step, (void *)(uintptr_t)setting);
    lv_obj_t *value = ui_text(row, 28, NUKA_COLOR_TEXT); lv_obj_set_width(value, 104); lv_obj_set_style_text_align(value, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_user_data(value, (void *)(uintptr_t)seconds); show_seconds(value, seconds);
    lv_obj_t *plus = ui_icon_button(row, ICON_PLUS, 56, false, timeout_step, (void *)(uintptr_t)setting);
    lv_group_add_obj(nav_focus_group(), minus); lv_group_add_obj(nav_focus_group(), plus);
}
static void network_info(lv_obj_t *page)
{
    for(unsigned i=0;i<3;++i){char key[8];snprintf(key,sizeof(key),"wifi%u",i);cJSON *wifi;
        esp_err_t error=store_read_json("prov",key,&wifi);if(error==ESP_ERR_NVS_NOT_FOUND)continue;
        if(error!=ESP_OK){nav_runtime_error(error);return;}
        const cJSON *ssid=cJSON_GetObjectItemCaseSensitive(wifi,"ssid");
        if(cJSON_IsString(ssid))ui_list_row(page,"SSID",ssid->valuestring);else nav_runtime_error(ESP_ERR_INVALID_ARG);
        cJSON_Delete(wifi);
    }
    char name[128];esp_err_t error=store_read_string("prov","hub_name",name,sizeof(name));
    if(error==ESP_OK)ui_list_row(page,"Hub",name);else if(error!=ESP_ERR_NVS_NOT_FOUND)nav_runtime_error(error);
    cJSON *hubs;error=store_read_json("prov","hubs",&hubs);if(error==ESP_ERR_NVS_NOT_FOUND)return;
    if(error!=ESP_OK){nav_runtime_error(error);return;}
    if(cJSON_IsArray(hubs)){const cJSON *item;cJSON_ArrayForEach(item,hubs){if(cJSON_IsString(item))ui_list_row(page,"",item->valuestring);else nav_runtime_error(ESP_ERR_INVALID_ARG);}}
    else nav_runtime_error(ESP_ERR_INVALID_ARG);
    cJSON_Delete(hubs);
}
// SPEC §6.8.11 groups: display, interaction, power, network, system.
static void detail(lv_obj_t *page)
{
    ui_section(page,tr("settings.section.display"));
    dropdown(page,tr("settings.language"),"简体中文\nEnglish",!strcmp(nuka_settings.lang,"en"),0);
    lv_obj_t *row=setting_row(page,tr("settings.brightness"));lv_obj_t *slider=lv_slider_create(row);lv_obj_set_size(slider,200,24);lv_slider_set_range(slider,1,100);
    lv_slider_set_value(slider,nuka_settings.brightness,LV_ANIM_OFF);lv_obj_add_event_cb(slider,changed,LV_EVENT_VALUE_CHANGED,(void *)1);lv_group_add_obj(nav_focus_group(),slider);
    timeout_control(page,tr("settings.dim_after"),nuka_settings.t_dim,2);timeout_control(page,tr("settings.off_after"),nuka_settings.t_off,3);
    ui_section(page,tr("settings.section.interaction"));
    dropdown(page,tr("settings.raise"),tr("options.off_on"),nuka_settings.raise,4);dropdown(page,tr("settings.flip"),tr("options.flip"),nuka_settings.flip_action,5);
    unsigned key_action=0,double_card=0;const char *actions[]={"talk","dnd","always_on","pomodoro","macro","none"};for(unsigned i=0;i<6;++i)if(!strcmp(actions[i],nuka_settings.key3_action))key_action=i;for(unsigned i=0;i<nuka_app_count;++i)if(!strcmp(nuka_apps[i]->id,nuka_settings.key3_double_card))double_card=i;
    dropdown(page,tr("settings.keys"),tr("options.key_action"),key_action,8);dropdown(page,tr("settings.macro"),"0\n1\n2\n3\n4\n5\n6\n7\n8\n9\n10\n11",nuka_settings.key3_macro,9);
    dropdown(page,tr("settings.double"),tr("options.apps"),double_card,10);dropdown(page,tr("settings.clock_style"),tr("options.off_on"),nuka_settings.clock_style,11);
    action_row(page,tr("settings.cards"),16,NUKA_COLOR_TEXT);
    ui_section(page,tr("settings.section.power"));
    dropdown(page,tr("settings.charge"),tr("options.charge"),nuka_settings.charge_mode,6);dropdown(page,tr("settings.current"),"100 mA\n200 mA\n300 mA\n400 mA\n500 mA",nuka_settings.charge_ma_index,7);
    char text[96];float screen,standby;battery_estimates(&screen,&standby);
    if(screen>=0&&standby>=0){snprintf(text,sizeof(text),"%.1f h / %.1f h",screen,standby);ui_list_row(page,tr("settings.battery"),text);}
    ui_section(page,tr("settings.vault"));
    vault_status_t v=vault_status();dropdown(page,tr("settings.vault_wipe"),tr("options.vault_wipe"),v.wipe_after==10?0:v.wipe_after==20?1:2,12);dropdown(page,tr("settings.vault_timeout"),"60 s\n120 s\n300 s\n600 s",v.unlock_seconds==60?0:v.unlock_seconds==120?1:v.unlock_seconds==300?2:3,13);
    ui_section(page,tr("settings.section.network"));
    network_info(page);
    action_row(page,tr("settings.wifi"),14,NUKA_COLOR_TEXT);
    ui_section(page,tr("settings.section.system"));
    action_row(page,tr("ota.check"),18,NUKA_COLOR_TEXT);
    dropdown(page,tr("settings.hud"),tr("options.off_on"),0,17);
    ui_list_row(page,tr("settings.version"),esp_app_get_description()->version);
    ui_list_row(page,tr("settings.assets"),nuka_assets_version());
    snprintf(text,sizeof(text),"%u KB",(unsigned)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)/1024));ui_list_row(page,tr("settings.memory"),text);
    size_t total,used;esp_err_t error=esp_littlefs_info("storage",&total,&used);
    if(error==ESP_OK){snprintf(text,sizeof(text),"%u / %u KB",(unsigned)(used/1024),(unsigned)(total/1024));ui_list_row(page,tr("settings.storage"),text);}else nav_runtime_error(error);
    ui_list_row(page,tr("settings.license"),"AGPL-3.0-or-later");
    action_row(page,tr("settings.factory"),15,NUKA_COLOR_DANGER);
}
const app_view_t settings_view={create,apply,detail};
