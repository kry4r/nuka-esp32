// SPDX-License-Identifier: AGPL-3.0-or-later
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdatomic.h>
#include "esp_log.h"
#include "esp_system.h"
#include "esp_task_wdt.h"
#include "esp_heap_caps.h"
#include "bootloader_random.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "store.h"
#include "nuka_events.h"
#include "vm_store.h"
#include "i2c_bus.h"
#include "axp2101.h"
#include "cst9220_touch.h"
#include "keys.h"
#include "qmi8658.h"
#include "nuka_time.h"
#include "nukapack.h"
#include "i18n.h"
#include "gfx.h"
#include "input.h"
#include "power.h"
#include "battery.h"
#include "net.h"
#include "hub_client.h"
#include "usb.h"
#include "vault.h"
#include "vault_app.h"
#include "pomodoro_app.h"
#include "pet_app.h"
#include "ota.h"
#include "diag.h"
#include "log.h"
#include "crash.h"
#include "audio_spike.h"
#include "theme.h"
#include "nav.h"
#include "keynav.h"
#include "overlay.h"
#include "perf_hud.h"
static bool assets_ready;
static QueueHandle_t ui_events;
static atomic_uint ui_updates;
static char ui_language[6];
static void *json_allocate(size_t size)
{ return heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT); }
typedef struct { int32_t id; union { key_event_t key; gesture_t gesture; char *text; }; } ui_event_t;
static void bus_event(void *arg,esp_event_base_t base,int32_t id,void *data)
{
    if(id==NUKA_SCREEN_CHANGED){hub_request_state();return;}
    if(id==NUKA_VM_CHANGED||id==NUKA_HUB_CONNECTED||id==NUKA_HUB_DISCONNECTED){atomic_fetch_or(&ui_updates,1);return;}
    if(id==NUKA_CONFIG_CHANGED){atomic_fetch_or(&ui_updates,2);return;}
    if(id==NUKA_PERMISSION){cJSON *frame=cJSON_Parse(data);bool pending=!strcmp(cJSON_GetObjectItemCaseSensitive(frame,"t")->valuestring,"perm.req");cJSON_Delete(frame);if(pending){power_activity();power_set_permission_pending(true);}}
    if(id==NUKA_USB_PAIR||id==NUKA_USB_VAULT)power_activity();
    if(id==NUKA_NOTIFICATION){cJSON *notification=cJSON_Parse(data);const char *level=cJSON_GetObjectItemCaseSensitive(notification,"level")->valuestring;
        bool wake=!nuka_settings.dnd&&(!strcmp(level,"critical")||cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(notification,"wake")));cJSON_Delete(notification);
        if(wake)power_activity();else if(!power_screen_on())return;
    }
    ui_event_t event={.id=id};
    if(id==NUKA_KEY)event.key=*(key_event_t *)data;
    else if(id==NUKA_PROVISIONING_READY)power_activity();
    else if(id==NUKA_GESTURE)event.gesture=*(gesture_t *)data;
    else if(id==NUKA_PERMISSION||id==NUKA_NOTIFICATION||id==NUKA_USB_PAIR||id==NUKA_USB_VAULT||id==NUKA_OTA_OFFER)event.text=strdup(data);
    else return;
    xQueueSend(ui_events,&event,portMAX_DELAY);
}
static void create_ui(void)
{
    if(!assets_ready){lv_obj_t *label=lv_label_create(lv_screen_active());lv_label_set_text(label,"nukapack ABI mismatch\nUpdate resource pack via USB or OTA.");lv_obj_set_width(label,400);lv_obj_center(label);return;}
    strcpy(ui_language,nuka_settings.lang);i18n_set_language(ui_language);theme_init();
    nav_init();overlay_init();keynav_init();perf_hud_init();gfx_set_touch_observer(nav_touch);
}
static void ota_decision(lv_event_t *event){ota_accept((intptr_t)lv_event_get_user_data(event));overlay_close();}
static void ui_tick(void)
{
    unsigned updates=atomic_exchange(&ui_updates,0);
    if(assets_ready&&updates&1)nav_refresh();
    if(updates&2){if(assets_ready){if(strcmp(ui_language,nuka_settings.lang)){strcpy(ui_language,nuka_settings.lang);i18n_set_language(ui_language);nav_reload();}else nav_refresh();}power_reload_settings();}
    ui_event_t event;
    while(xQueueReceive(ui_events,&event,0)){
        if(event.id==NUKA_KEY){
            if(event.key.kind==KEYEV_POWER_OFF)power_shutdown("user");
            if(!assets_ready)continue;
            keynav_handle(&event.key);
        }else if(event.id==NUKA_GESTURE){
            if(!assets_ready)continue;
            pet_gesture(event.gesture);
            if(!strcmp(nav_current()->id,"pomodoro"))pomodoro_gesture(event.gesture);
            if(event.gesture==GESTURE_FACE_DOWN){if(nuka_settings.flip_action==0)power_screen_off();if(nuka_settings.flip_action==1)nuka_settings.dnd=true;}
        }else if(event.id==NUKA_PROVISIONING_READY){if(assets_ready)overlay_provisioning();}
        else if(event.id==NUKA_VM_CHANGED||event.id==NUKA_HUB_CONNECTED||event.id==NUKA_HUB_DISCONNECTED){if(assets_ready)nav_refresh();}
        else if(event.id==NUKA_CONFIG_CHANGED){if(assets_ready){i18n_set_language(nuka_settings.lang);nav_reload();}power_reload_settings();}
        else{
            cJSON *data=cJSON_Parse(event.text);free(event.text);
            if(assets_ready){
                if(event.id==NUKA_PERMISSION){const char *type=cJSON_GetObjectItemCaseSensitive(data,"t")->valuestring;if(!strcmp(type,"perm.req"))overlay_permission(cJSON_GetObjectItemCaseSensitive(data,"d"));else overlay_permission_done(cJSON_GetObjectItemCaseSensitive(data,"d"));}
                if(event.id==NUKA_NOTIFICATION)overlay_notification(data);
                if(event.id==NUKA_USB_PAIR||event.id==NUKA_USB_VAULT){const cJSON *action=cJSON_GetObjectItemCaseSensitive(data,"action");if(action&&!strcmp(action->valuestring,"closed"))overlay_usb_closed(event.id==NUKA_USB_VAULT);else if(event.id==NUKA_USB_PAIR)overlay_pair(data);else overlay_vault(data);}
                if(event.id==NUKA_OTA_OFFER)overlay_ota(data,ota_decision);
            }
            cJSON_Delete(data);
        }
    }
    if(!assets_ready)return;
    vault_app_tick();nav_tick();overlay_tick();perf_hud_tick();
}
void app_main(void)
{
    cJSON_Hooks json_hooks={.malloc_fn=json_allocate,.free_fn=heap_caps_free};
    cJSON_InitHooks(&json_hooks);
    ESP_LOGI("boot","1/12 NVS settings");ESP_ERROR_CHECK(store_init());nuka_log_init();
    ESP_LOGI("boot","2/12 reset reason=%d boot=%lu",esp_reset_reason(),(unsigned long)nuka_settings.boot_count);
    crash_report();
    if(esp_reset_reason()==ESP_RST_TASK_WDT)ESP_ERROR_CHECK(store_write_string("sys","wdt_reason","task_watchdog"));
    ESP_LOGI("boot","3/12 nuka_bus and vm_store");ESP_ERROR_CHECK(nuka_bus_init());vm_store_init();power_init();hub_client_init();ui_events=xQueueCreate(32,sizeof(ui_event_t));ESP_ERROR_CHECK(ui_events?ESP_OK:ESP_ERR_NO_MEM);ESP_ERROR_CHECK(esp_event_handler_register_with(nuka_bus,NUKA_EVENTS,ESP_EVENT_ANY_ID,bus_event,NULL));
    ESP_LOGI("boot","4/12 I2C and AXP2101");ESP_ERROR_CHECK(i2c_bus_init());axp_cfg_t axp={.charge_ma=(unsigned[]){100,200,300,400,500}[nuka_settings.charge_ma_index],.full_charge=nuka_settings.charge_mode};ESP_ERROR_CHECK(axp_init(&axp));
    ESP_LOGI("boot","5/12 CO5300 and DMA display");ESP_ERROR_CHECK(gfx_init());
    ESP_LOGI("boot","6/12 map nukapack");esp_err_t error=nukapack_init();assets_ready=error==ESP_OK;if(!assets_ready)ESP_LOGE("assets","%s",esp_err_to_name(error));
    ESP_LOGI("boot","7/12 touch, keys, IMU, RTC, Vault");ESP_ERROR_CHECK(touch_init());ESP_ERROR_CHECK(keys_init());ESP_ERROR_CHECK(imu_init());ESP_ERROR_CHECK(nuka_time_init());bootloader_random_enable();ESP_ERROR_CHECK(vault_init());bootloader_random_disable();ESP_ERROR_CHECK(pomodoro_init());diag_start();
    const esp_task_wdt_config_t watchdog={.timeout_ms=10000,.idle_core_mask=3,.trigger_panic=true};ESP_ERROR_CHECK(esp_task_wdt_reconfigure(&watchdog));
    ESP_LOGI("boot","8/12 create UI in lvgl task");gfx_start(create_ui,ui_tick);
    ESP_LOGI("boot","9/12 power, input and PMU tasks");power_start();input_start();battery_start();
    ESP_LOGI("boot","10/12 USB CDC and HID");usb_start();power_usb_ready();
    ESP_LOGI("boot","11/12 Wi-Fi and Hub client");ota_start();net_start();hub_client_start();
    ESP_LOGI("boot","12/12 OTA handshake confirmation pending when applicable");
    audio_spike_start();
    ESP_LOGI("boot","internal free=%u minimum=%u",(unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),(unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL));
}
