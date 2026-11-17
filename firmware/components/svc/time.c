// SPDX-License-Identifier: AGPL-3.0-or-later
#include "nuka_time.h"
#include <stdbool.h>
#include <stdlib.h>
#include <sys/time.h>
#include "pcf85063.h"
#include "esp_sntp.h"
#include "nuka_events.h"
#include "store.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include <string.h>
static time_t last_sync;
static portMUX_TYPE sync_lock = portMUX_INITIALIZER_UNLOCKED;
static QueueHandle_t pending;
static char *ntp_servers[CONFIG_LWIP_SNTP_MAX_SERVERS];
static void synced(struct timeval *tv)
{
    time_t epoch = tv->tv_sec; xQueueOverwrite(pending, &epoch);
}
static esp_err_t save_sync(time_t epoch)
{
    struct tm utc; gmtime_r(&epoch, &utc);
    esp_err_t error = rtc_set(&utc);
    if (error == ESP_OK) error = store_write_u32("sys", "time_sync", epoch);
    if (error != ESP_OK) return error;
    portENTER_CRITICAL(&sync_lock); last_sync = epoch; portEXIT_CRITICAL(&sync_lock);
    nuka_post(NUKA_TIME_CHANGED, NULL, 0);
    return ESP_OK;
}
esp_err_t nuka_time_init(void)
{
    pending = xQueueCreate(1, sizeof(time_t));
    ESP_ERROR_CHECK(pending ? ESP_OK : ESP_ERR_NO_MEM);
    struct tm utc; esp_err_t err = rtc_get(&utc);
    bool valid = err != ESP_ERR_INVALID_STATE;
    if (valid) ESP_ERROR_CHECK(err);
    ESP_ERROR_CHECK(setenv("TZ", "UTC0", 1) == 0 ? ESP_OK : ESP_FAIL); tzset();
    if (valid) {
        struct timeval tv = {.tv_sec = mktime(&utc)};
        ESP_ERROR_CHECK(settimeofday(&tv, NULL) == 0 ? ESP_OK : ESP_FAIL);
    }
    char timezone[128]; esp_err_t stored = store_read_string("sys", "tz_posix", timezone, sizeof(timezone));
    if (stored == ESP_OK) {
        for (const unsigned char *p = (const unsigned char *)timezone; *p; ++p)
            if (*p < 32 || *p > 126) return ESP_ERR_INVALID_ARG;
        ESP_ERROR_CHECK(setenv("TZ", timezone, 1) == 0 ? ESP_OK : ESP_FAIL); tzset();
    }
    else if (stored != ESP_ERR_NVS_NOT_FOUND) return stored;
    uint32_t saved; stored = store_read_u32("sys", "time_sync", &saved);
    if (stored == ESP_OK) {
        if (saved && (saved < 946684800U || saved > 4102444799U)) return ESP_ERR_INVALID_ARG;
        if (valid) last_sync = saved;
    }
    else if (stored != ESP_ERR_NVS_NOT_FOUND) return stored;
    return ESP_OK;
}
esp_err_t nuka_time_update(time_t epoch, const char *tz_posix)
{
    struct timeval tv = {.tv_sec = epoch};
    if (settimeofday(&tv, NULL) != 0 || setenv("TZ", tz_posix, 1) != 0) return ESP_FAIL;
    tzset(); xQueueReset(pending);
    esp_err_t error = store_write_string("sys", "tz_posix", tz_posix);
    return error == ESP_OK ? save_sync(epoch) : error;
}
esp_err_t nuka_time_sntp(const char *const *servers, unsigned count)
{
    if (esp_sntp_enabled()) esp_sntp_stop();
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    for (unsigned i = 0; i < CONFIG_LWIP_SNTP_MAX_SERVERS; ++i) {
        free(ntp_servers[i]);
        ntp_servers[i] = i < count ? strdup(servers[i]) : NULL;
        esp_sntp_setservername(i, ntp_servers[i]);
        if (i < count && !ntp_servers[i]) return ESP_ERR_NO_MEM;
    }
    esp_sntp_set_time_sync_notification_cb(synced); esp_sntp_init();
    return ESP_OK;
}
esp_err_t nuka_time_poll(void)
{
    time_t epoch;
    if (!xQueueReceive(pending, &epoch, 0)) return ESP_OK;
    if (epoch < 946684800LL || epoch > 4102444799LL) return ESP_ERR_INVALID_ARG;
    return save_sync(epoch);
}
bool nuka_time_stale(void)
{
    portENTER_CRITICAL(&sync_lock); time_t synced_at = last_sync; portEXIT_CRITICAL(&sync_lock);
    return synced_at == 0 || time(NULL) - synced_at > 7 * 86400;
}
