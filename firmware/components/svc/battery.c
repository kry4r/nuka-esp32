// SPDX-License-Identifier: AGPL-3.0-or-later
#include "battery.h"
#include "power.h"
#include "store.h"
#include "nuka_events.h"
#include "qmi8658.h"
#include "imu_gesture.h"
#include "i18n.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <math.h>
#include <time.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
static axp_status_t status;
static float filtered;
static bool initialized, low_warned, full_notified;
static int64_t transition_at;
typedef struct { int64_t at; float pct; } battery_point_t;
static battery_point_t *history;
static unsigned history_head, history_count;
static int eta_minutes = -1;
static float screen_rate, standby_rate;
static struct { double screen_pct,standby_pct;int64_t screen_s,standby_s; } usage;
static int64_t previous_at,last_saved;
static float previous_pct;
static bool previous_screen;
static esp_err_t save_usage(void)
{
    FILE *file=fopen("/storage/battery.new","wb");if(!file)return ESP_FAIL;
    bool written=fwrite(&usage,sizeof(usage),1,file)==1;
    written&=fclose(file)==0;if(!written)return ESP_FAIL;
    return rename("/storage/battery.new","/storage/battery")==0?ESP_OK:ESP_FAIL;
}
static void notify(const char *title)
{
    cJSON *d = cJSON_CreateObject();
    cJSON_AddStringToObject(d, "nid", "battery"); cJSON_AddStringToObject(d, "level", "warn");
    cJSON_AddStringToObject(d, "src", "battery"); cJSON_AddStringToObject(d, "title", title);
    cJSON_AddNumberToObject(d, "ts", time(NULL)); cJSON_AddBoolToObject(d, "wake", true);
    char *text = cJSON_PrintUnformatted(d); nuka_post(NUKA_NOTIFICATION, text, strlen(text) + 1); cJSON_free(text); cJSON_Delete(d);
}
static esp_err_t sample(axp_status_t next)
{
    int64_t now = esp_timer_get_time() / 1000000;
    bool transition = initialized && (next.charging != status.charging || next.vbus_present != status.vbus_present);
    if (!initialized || transition) {
        filtered = next.pct; transition_at = now; history_head = history_count = 0;
    } else filtered = filtered * 0.9f + next.pct * 0.1f;
    if (initialized && now - transition_at < 30) next.pct = status.pct;
    else if (initialized && !transition) {
        uint8_t pct = lroundf(filtered);
        next.pct = next.charging ? (pct > status.pct ? pct : status.pct) : (pct < status.pct ? pct : status.pct);
    }
    history[history_head] = (battery_point_t){now, filtered};
    history_head = (history_head + 1) % 600;
    if (history_count < 600) ++history_count;
    unsigned oldest = (history_head + 600 - history_count) % 600;
    float delta = filtered - history[oldest].pct;
    int64_t elapsed = now - history[oldest].at;
    eta_minutes = -1;
    if (elapsed >= 60 && fabsf(delta) >= 0.5f) {
        float rate = fabsf(delta) / elapsed;
        eta_minutes = (next.charging ? 100 - filtered : filtered) / rate / 60;
    }
    bool screen=power_screen_on();
    if(initialized&&!transition&&!next.charging&&next.batt_present&&screen==previous_screen&&now-transition_at>=30){
        double spent=previous_pct-filtered;
        if(spent>=0){if(screen){usage.screen_s+=now-previous_at;usage.screen_pct+=spent;}else{usage.standby_s+=now-previous_at;usage.standby_pct+=spent;}}
    }
    previous_at=now;previous_pct=filtered;previous_screen=screen;
    screen_rate=usage.screen_s>=60?usage.screen_pct/usage.screen_s:0;
    standby_rate=usage.standby_s>=60?usage.standby_pct/usage.standby_s:0;
    status = next; initialized = true;
    power_set_battery_limits(status.batt_present ? status.pct : 100, status.charging, status.vbus_present);
    if (status.batt_present && status.pct < 10 && !status.vbus_present && !status.charging && !low_warned) { notify(tr("power.battery_low")); low_warned = true; }
    if (status.charging) low_warned = false;
    if (status.full && !full_notified) { notify(tr("power.charged")); full_notified = true; }
    if (!status.full) full_notified = false;
    uint16_t current = status.die_temp > 60 ? 100 : (unsigned[]){100,200,300,400,500}[nuka_settings.charge_ma_index];
    esp_err_t err=axp_set_charge(current,nuka_settings.charge_mode);
    if(err!=ESP_OK)return err;
    if(now-last_saved>=60){err=save_usage();if(err!=ESP_OK)return err;last_saved=now;}
    if (status.die_temp > 75) power_shutdown("over_temperature");
    if (status.pct < 5 && !status.charging && status.batt_present) {
        notify(tr("power.battery_low")); vTaskDelay(pdMS_TO_TICKS(3000)); power_shutdown("low_battery");
    }
    nuka_post(NUKA_BATTERY_CHANGED, &status, sizeof(status));return ESP_OK;
}
static void pmu_task(void *arg)
{
    int64_t next_battery = 0;
    for (;;) {
        int64_t now = esp_timer_get_time();
        if (now >= next_battery) {
            axp_status_t next; esp_err_t err = axp_read_status(&next);
            if(err==ESP_OK)err=sample(next);
            if(err!=ESP_OK)ESP_LOGE("pmu","read/charge: %s",esp_err_to_name(err));
            next_battery = now + (power_screen_on() ? 1000000 : 10000000);
        }
        if (power_screen_on() || imu_motion_pending()) {
            imu_sample_t imu; esp_err_t err = imu_read(&imu);
            if (err == ESP_OK) imu_gesture_sample(&imu); else ESP_LOGE("pmu", "imu: %s", esp_err_to_name(err));
        }
        vTaskDelay(pdMS_TO_TICKS(40));
    }
}
void battery_start(void)
{
    history=heap_caps_calloc(600,sizeof(*history),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    ESP_ERROR_CHECK(history?ESP_OK:ESP_ERR_NO_MEM);
    FILE *file=fopen("/storage/battery","rb");
    if(file){bool read=fread(&usage,sizeof(usage),1,file)==1;read&=fclose(file)==0;ESP_ERROR_CHECK(read?ESP_OK:ESP_FAIL);}
    else ESP_ERROR_CHECK(errno==ENOENT?ESP_OK:ESP_FAIL);
    ESP_ERROR_CHECK(xTaskCreatePinnedToCore(pmu_task,"pmu_poll",3072,NULL,3,NULL,0)==pdPASS?ESP_OK:ESP_ERR_NO_MEM);
}
axp_status_t battery_status(void) { return status; }
bool battery_low_power(void) { return status.batt_present && !status.vbus_present && status.pct < 10 && !status.charging; }
cJSON *battery_json(void)
{
    cJSON *d = cJSON_CreateObject();
    cJSON_AddNumberToObject(d, "pct", status.pct); cJSON_AddNumberToObject(d, "mv", status.vbat_mv);
    cJSON_AddStringToObject(d, "chg", status.full ? "full" : status.charging ? "charging" : "none");
    cJSON_AddStringToObject(d, "src", status.vbus_present ? "usb" : "bat");
    if (eta_minutes >= 0) cJSON_AddNumberToObject(d, "eta_min", eta_minutes);
    return d;
}
void battery_estimates(float *screen_hours, float *standby_hours)
{
    *screen_hours = screen_rate > 0 ? status.pct / screen_rate / 3600 : -1;
    *standby_hours = standby_rate > 0 ? status.pct / standby_rate / 3600 : -1;
}
