// SPDX-License-Identifier: AGPL-3.0-or-later
#include "pet_app.h"
#include "battery.h"
#include "esp_timer.h"
#include <string.h>
#include <time.h>
static int64_t dizzy_until, celebrate_until;
void pet_gesture(gesture_t gesture)
{
    if(gesture==GESTURE_SHAKE)dizzy_until=esp_timer_get_time()+1500000;
    if(gesture==GESTURE_POMO_DONE)celebrate_until=esp_timer_get_time()+5000000;
}
const char *pet_mood(const cJSON *vm)
{
    const cJSON *mood=cJSON_GetObjectItemCaseSensitive(vm,"mood");const char *hub=mood?mood->valuestring:"idle";
    if(!strcmp(hub,"attention")||!strcmp(hub,"hungry"))return hub;
    if(esp_timer_get_time()<dizzy_until)return "dizzy";
    if(esp_timer_get_time()<celebrate_until)return "celebrate";
    if(!strcmp(hub,"celebrate"))return hub;
    if(!strcmp(hub,"busy"))return hub;
    if(battery_status().charging)return "charging";
    return hub;
}
