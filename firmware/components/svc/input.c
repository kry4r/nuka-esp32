// SPDX-License-Identifier: AGPL-3.0-or-later
#include "input.h"
#include "power.h"
#include "nuka_events.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
typedef struct {
    bool raw, down, long_sent, menu_sent, off_sent, swallowed;
    int64_t changed, started, pending_short;
} key_state_t;
static void emit(key_id_t key, key_event_kind_t kind, int64_t held)
{
    const key_event_t event = {.key = key, .kind = kind, .held_ms = held};
    if (kind != KEYEV_HOLD&&kind!=KEYEV_COMBO_HOLD) ESP_LOGI("input", "key=%d event=%d held=%d", key, kind, (int)held);
    nuka_post(NUKA_KEY, &event, sizeof(event));
}
static void input_task(void *arg)
{
    key_state_t states[KEY_COUNT] = {0}; int64_t combo_start = 0,combo_hold=0,last_hold[KEY_COUNT]={0}; bool combo_sent = false;
    for (;;) {
        int64_t now = esp_timer_get_time() / 1000;
        bool combo = keys_pressed(KEY_BOOT) && keys_pressed(KEY_KEY3);
        if (combo && !combo_start) combo_start = now;
        if(combo&&!combo_sent&&now-combo_hold>=50){emit(KEY_BOOT,KEYEV_COMBO_HOLD,now-combo_start);combo_hold=now;}
        if (combo && !combo_sent && now - combo_start >= 3000) {
            emit(KEY_BOOT, KEYEV_RECOVERY, 3000); combo_sent = true;
        }
        for (int i = 0; i < KEY_COUNT; ++i) {
            key_state_t *s = &states[i]; bool raw = keys_pressed(i);
            if (raw != s->raw) { s->raw = raw; s->changed = now; }
            if (now - s->changed >= 20 && s->down != raw) {
                s->down = raw;
                if (raw) {
                    s->started = now; s->long_sent = s->menu_sent = s->off_sent = false;
                    s->swallowed = !power_screen_on(); power_activity();
                    emit(i, KEYEV_PRESS, 0);
                } else {
                    int64_t held = now - s->started;
                    emit(i, KEYEV_RELEASE, held);
                    if (!s->swallowed && !combo_start && held < (i == KEY_KEY3 ? 400 : 600) && !s->long_sent) {
                        if (i == KEY_KEY3) {
                            if (s->pending_short && now - s->pending_short < 300) {
                                emit(i, KEYEV_DOUBLE, held); s->pending_short = 0;
                            } else s->pending_short = now;
                        } else emit(i, KEYEV_SHORT, held);
                    }
                }
            }
            if (i == KEY_KEY3 && s->pending_short && now - s->pending_short >= 300) {
                emit(i, KEYEV_SHORT, 0); s->pending_short = 0;
            }
            if (!s->down || combo || (s->swallowed && i != KEY_PWR && i != KEY_KEY3)) continue;
            int64_t held = now - s->started;
            if (i != KEY_PWR && held >= 1000 && !s->long_sent) {
                emit(i, KEYEV_LONG, held); s->long_sent = true;
            }
            if (i == KEY_PWR && held >= 2000 && !s->menu_sent) {
                emit(i, KEYEV_POWER_MENU, held); s->menu_sent = true; s->long_sent = true;
            }
            if (i == KEY_PWR && held >= 4000 && !s->off_sent) {
                emit(i, KEYEV_POWER_OFF, held); s->off_sent = true;
            }
            if(now-last_hold[i]>=50){emit(i, KEYEV_HOLD, held);last_hold[i]=now;}
        }
        if (!combo && !states[KEY_BOOT].down && !states[KEY_KEY3].down) {
            combo_start = 0; combo_sent = false;
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
void input_start(void) { ESP_ERROR_CHECK(xTaskCreatePinnedToCore(input_task,"input",3072,NULL,6,NULL,1)==pdPASS?ESP_OK:ESP_ERR_NO_MEM); }
