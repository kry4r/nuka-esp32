// SPDX-License-Identifier: AGPL-3.0-or-later
#include "power.h"
#include "gfx.h"
#include "store.h"
#include "input.h"
#include "nuka_events.h"
#include "axp2101.h"
#include "qmi8658.h"
#include "vault.h"
#include "hub_client.h"
#include "pomodoro_app.h"
#include "esp_log.h"
#include "pins.h"
#include "esp_pm.h"
#include "esp_sleep.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include <string.h>
#include <stdatomic.h>
static power_state_t state = POWER_STARTING;
static int64_t last_activity;
static atomic_uint screen_off_at_s;
static bool pending_permission;
static bool low_battery, usb_power;
static uint8_t brightness_cap = 100;
static QueueHandle_t commands;
static esp_pm_lock_handle_t usb_lock;
static bool usb_ready,usb_lock_held=true;
static char shutdown_reason[32];
enum { CMD_ACTIVITY, CMD_OFF, CMD_SHUTDOWN, CMD_CONFIG };
static void screen_changed(void)
{
    bool on = power_screen_on(); nuka_post(NUKA_SCREEN_CHANGED, &on, sizeof(on));
}
static void power_task(void *arg)
{
    state = POWER_ACTIVE; last_activity = esp_timer_get_time();
    ESP_ERROR_CHECK(gfx_fade_to(nuka_settings.brightness, 200));
    for (;;) {
        esp_err_t error=vault_tick();
        if(error==ESP_OK)error=pomodoro_tick();
        int cmd = -1; xQueueReceive(commands, &cmd, pdMS_TO_TICKS(50));
        int64_t now = esp_timer_get_time();
        bool need_usb_lock=!usb_ready||usb_power;
        if(need_usb_lock!=usb_lock_held){error=need_usb_lock?esp_pm_lock_acquire(usb_lock):esp_pm_lock_release(usb_lock);if(error==ESP_OK)usb_lock_held=need_usb_lock;}
        if (state == POWER_SCREEN_OFF && gpio_get_level(PIN_TOUCH_INT) == 0) cmd = CMD_ACTIVITY;
        if (cmd == CMD_SHUTDOWN) {
            bool screen_on = power_screen_on();
            state = POWER_SHUTDOWN;error=store_write_string("sys","wdt_reason",shutdown_reason);if(error==ESP_OK)error=store_save_settings(); hub_disconnect(); vault_lock();
            if (screen_on&&error==ESP_OK) error=gfx_suspend();
            axp_power_off();
        }
        if (cmd == CMD_ACTIVITY) {
            last_activity = now;
            if (state == POWER_SCREEN_OFF) {
                error=imu_set_sleep(false);if(error==ESP_OK)error=gfx_resume();state = POWER_ACTIVE;
                esp_wifi_set_ps(WIFI_PS_MIN_MODEM);
                if(error==ESP_OK)error=gfx_fade_to(nuka_settings.brightness < brightness_cap ? nuka_settings.brightness : brightness_cap, 200);
                screen_changed();
            } else if (state == POWER_DIM) {
                state = POWER_ACTIVE; if(error==ESP_OK)error=gfx_fade_to(nuka_settings.brightness < brightness_cap ? nuka_settings.brightness : brightness_cap, 200);
            }
        }
        if(cmd==CMD_CONFIG&&state==POWER_ACTIVE&&error==ESP_OK)error=gfx_fade_to(nuka_settings.brightness<brightness_cap?nuka_settings.brightness:brightness_cap,200);
        uint32_t off_s = pending_permission ? 60 : nuka_settings.t_off / (low_battery ? 2 : 1);
        uint32_t dim_s = nuka_settings.t_dim / (low_battery ? 2 : 1);
        bool always = nuka_settings.always_on && usb_power;
        if (state == POWER_ACTIVE && !always && now - last_activity >= dim_s * 1000000LL) {
            state = POWER_DIM; if(error==ESP_OK)error=gfx_fade_to(nuka_settings.brightness < 20 ? nuka_settings.brightness : 20, 200);
        }
        if ((cmd == CMD_OFF && power_screen_on()) || (state == POWER_DIM && !always &&
            now - last_activity >= (dim_s + off_s) * 1000000LL)) {
            if(error==ESP_OK)error=gfx_suspend();
            vault_lock();if(error==ESP_OK)error=imu_set_sleep(true);
            atomic_store(&screen_off_at_s,(unsigned)(esp_timer_get_time()/1000000));state = POWER_SCREEN_OFF;
            esp_wifi_set_ps(WIFI_PS_MAX_MODEM); screen_changed();
        }
        if(error!=ESP_OK)ESP_LOGE("power","operation: %s",esp_err_to_name(error));
    }
}
void power_start(void)
{
    ESP_ERROR_CHECK(esp_pm_lock_create(ESP_PM_NO_LIGHT_SLEEP,0,"usb_vbus",&usb_lock));
    ESP_ERROR_CHECK(esp_pm_lock_acquire(usb_lock));
    esp_pm_config_t pm = {.max_freq_mhz = 240, .min_freq_mhz = 80, .light_sleep_enable = true};
    ESP_ERROR_CHECK(esp_pm_configure(&pm));
    ESP_ERROR_CHECK(gpio_wakeup_enable(PIN_TOUCH_INT, GPIO_INTR_LOW_LEVEL));
    ESP_ERROR_CHECK(gpio_wakeup_enable(PIN_PWR, GPIO_INTR_HIGH_LEVEL));
    ESP_ERROR_CHECK(gpio_wakeup_enable(PIN_BOOT, GPIO_INTR_LOW_LEVEL));
    ESP_ERROR_CHECK(gpio_wakeup_enable(PIN_KEY3, GPIO_INTR_LOW_LEVEL));
    ESP_ERROR_CHECK(gpio_wakeup_enable(PIN_IMU_INT1, GPIO_INTR_HIGH_LEVEL));
    ESP_ERROR_CHECK(esp_sleep_enable_gpio_wakeup());
    ESP_ERROR_CHECK(xTaskCreatePinnedToCore(power_task,"power",4096,NULL,5,NULL,0)==pdPASS?ESP_OK:ESP_ERR_NO_MEM);
}
void power_usb_ready(void) { axp_status_t status;ESP_ERROR_CHECK(axp_read_status(&status));usb_power=status.vbus_present;usb_ready=true; }
void power_init(void) { commands=xQueueCreate(8,sizeof(int));ESP_ERROR_CHECK(commands?ESP_OK:ESP_ERR_NO_MEM); }
void power_reload_settings(void) { int cmd=CMD_CONFIG;xQueueSend(commands,&cmd,0); }
void power_activity(void) { int cmd = CMD_ACTIVITY; xQueueSend(commands, &cmd, 0); }
void power_screen_off(void) { int cmd = CMD_OFF; xQueueSend(commands, &cmd, portMAX_DELAY); }
bool power_screen_on(void) { return state != POWER_SCREEN_OFF && state != POWER_SHUTDOWN; }
uint32_t power_screen_off_seconds(void)
{ return state==POWER_SCREEN_OFF?(unsigned)(esp_timer_get_time()/1000000)-atomic_load(&screen_off_at_s):0; }
void power_shutdown(const char *reason)
{
    strcpy(shutdown_reason,reason);
    int cmd = CMD_SHUTDOWN; xQueueSend(commands, &cmd, portMAX_DELAY);
}
void power_set_permission_pending(bool pending) { pending_permission = pending; }
void power_set_battery_limits(unsigned pct, bool charging, bool usb)
{
    uint8_t old_cap=brightness_cap;
    low_battery = pct < 20 && !charging; usb_power = usb;
    brightness_cap = low_battery ? 50 : nuka_settings.always_on && usb ? 40 : 100;
    if(old_cap!=brightness_cap)power_reload_settings();
}
