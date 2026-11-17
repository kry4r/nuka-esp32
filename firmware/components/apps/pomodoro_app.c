// SPDX-License-Identifier: AGPL-3.0-or-later
#include "pomodoro_app.h"
#include "hub_client.h"
#include "store.h"
#include "power.h"
#include "audio.h"
#include "nuka_events.h"
#include "esp_timer.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <time.h>
extern const app_view_t pomodoro_view;
static unsigned minutes[] = {25, 5, 15}, phase, round_number = 1, left = 25 * 60;
static bool running, previous_dnd, flip_dnd;
static int64_t ends;
static StaticSemaphore_t mutex_storage;
static SemaphoreHandle_t mutex;
static const char *phases[] = {"focus", "short_break", "long_break"};

static pomodoro_status_t snapshot(int64_t now)
{
    return (pomodoro_status_t){ .phase = phases[phase], .running = running,
        .remaining = running ? (ends > now ? (unsigned)((ends - now + 999999) / 1000000) : 0) : left,
        .round = round_number, .minutes = {minutes[0], minutes[1], minutes[2]} };
}
pomodoro_status_t pomodoro_status(void)
{
    xSemaphoreTakeRecursive(mutex, portMAX_DELAY);
    pomodoro_status_t status = snapshot(esp_timer_get_time());
    xSemaphoreGiveRecursive(mutex); return status;
}
esp_err_t pomodoro_init(void)
{
    mutex = xSemaphoreCreateRecursiveMutexStatic(&mutex_storage);
    ESP_ERROR_CHECK(mutex ? ESP_OK : ESP_ERR_NO_MEM);
    cJSON *config;
    esp_err_t error = store_read_json("pomo", "cfg", &config);
    if (error == ESP_ERR_NVS_NOT_FOUND) return ESP_OK;
    if (error != ESP_OK) return error;
    if (!cJSON_IsArray(config) || cJSON_GetArraySize(config) != 3) error = ESP_ERR_INVALID_ARG;
    for (unsigned i = 0; error == ESP_OK && i < 3; ++i) {
        const cJSON *value = cJSON_GetArrayItem(config, i);
        if (!cJSON_IsNumber(value) || value->valuedouble < 1 || value->valuedouble > 180 ||
            value->valuedouble != value->valueint) error = ESP_ERR_INVALID_ARG;
        else minutes[i] = value->valueint;
    }
    cJSON_Delete(config); left = minutes[0] * 60; return error;
}
static void report(const char *state, pomodoro_status_t status)
{
    cJSON *data = cJSON_CreateObject(); cJSON_AddStringToObject(data, "phase", status.phase);
    cJSON_AddStringToObject(data, "state", state); cJSON_AddNumberToObject(data, "round", status.round);
    if (status.running) cJSON_AddNumberToObject(data, "ends_at", time(NULL) + status.remaining);
    hub_send("pomo", data, false);
}
static const char *toggle(int64_t now)
{
    if (running) { left = snapshot(now).remaining; running = false; return "paused"; }
    ends = now + (int64_t)left * 1000000; running = true; return "running";
}
static void action(app_action_t action)
{
    if (action != ACT_PRIMARY && action != ACT_SECONDARY) return;
    xSemaphoreTakeRecursive(mutex, portMAX_DELAY);
    int64_t now = esp_timer_get_time(); const char *event;
    if (action == ACT_PRIMARY) event = toggle(now);
    else { running = false; phase = 0; round_number = 1; left = minutes[0] * 60; event = "reset"; }
    pomodoro_status_t status = snapshot(now);
    xSemaphoreGiveRecursive(mutex); report(event, status);
}
esp_err_t pomodoro_tick(void)
{
    int64_t now = esp_timer_get_time();
    xSemaphoreTakeRecursive(mutex, portMAX_DELAY);
    bool completed = running && now >= ends;
    pomodoro_status_t done = snapshot(now);
    if (completed) {
        running = false;
        if (!phase) phase = round_number % 4 ? 1 : 2;
        else { phase = 0; ++round_number; }
        left = minutes[phase] * 60;
    }
    xSemaphoreGiveRecursive(mutex);
    if (completed) {
        done.running = false; power_activity();
        esp_err_t error = audio_tone(); if (error != ESP_OK) return error;
        nuka_post(NUKA_GESTURE, &(gesture_t){GESTURE_POMO_DONE}, sizeof(gesture_t));
        report("done", done);
    }
    return ESP_OK;
}
void pomodoro_gesture(gesture_t gesture)
{
    xSemaphoreTakeRecursive(mutex, portMAX_DELAY);
    bool change = gesture == GESTURE_STILL && !running && phase == 0;
    if (gesture == GESTURE_FACE_DOWN) {
        change = running;
        if (!flip_dnd) { previous_dnd = nuka_settings.dnd; flip_dnd = true; }
        nuka_settings.dnd = true;
    }
    if (gesture == GESTURE_RAISE && flip_dnd) { nuka_settings.dnd = previous_dnd; flip_dnd = false; }
    const char *event = change ? toggle(esp_timer_get_time()) : NULL;
    pomodoro_status_t status = snapshot(esp_timer_get_time());
    xSemaphoreGiveRecursive(mutex);
    if (change) report(event, status);
}
esp_err_t pomodoro_set_minutes(unsigned focus, unsigned short_break, unsigned long_break)
{
    if (focus < 1 || focus > 180 || short_break < 1 || short_break > 180 || long_break < 1 || long_break > 180)
        return ESP_ERR_INVALID_ARG;
    cJSON *data = cJSON_CreateIntArray((int[]){focus, short_break, long_break}, 3);
    esp_err_t error = store_write_json("pomo", "cfg", data); cJSON_Delete(data);
    if (error != ESP_OK) return error;
    xSemaphoreTakeRecursive(mutex, portMAX_DELAY);
    minutes[0] = focus; minutes[1] = short_break; minutes[2] = long_break;
    if (!running) left = minutes[phase] * 60;
    xSemaphoreGiveRecursive(mutex); return ESP_OK;
}
const app_def_t pomodoro_app = {"pomodoro", VM_NONE, &pomodoro_view, action};
