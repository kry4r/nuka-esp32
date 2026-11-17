// SPDX-License-Identifier: AGPL-3.0-or-later
#include "imu_gesture.h"
#include "nuka_events.h"
#include "store.h"
#include "power.h"
#include "battery.h"
#include "esp_timer.h"
#include <math.h>
static int64_t tilted_at, down_since, still_since, last_shake;
static bool face_down, still, peak;
static int64_t peaks[3];
static unsigned peak_index;
static float mean = 1, variance;
static void emit(gesture_t gesture) { nuka_post(NUKA_GESTURE, &gesture, sizeof(gesture)); }
void imu_gesture_sample(const imu_sample_t *s)
{
    int64_t now = esp_timer_get_time();
    float magnitude = sqrtf(s->x * s->x + s->y * s->y + s->z * s->z);
    float cosine = magnitude > 0 ? s->z / magnitude : 0;
    if (cosine < 0.5f) tilted_at = now;
    if (cosine > 0.766044f && now - tilted_at < 1000000 && tilted_at &&
        nuka_settings.raise && !battery_low_power() && !power_screen_on()) {
        tilted_at = 0; power_activity(); emit(GESTURE_RAISE);
    }
    if (s->z < -0.8f) {
        if (!down_since) down_since = now;
        if (!face_down && now - down_since >= 1500000) { face_down = true; emit(GESTURE_FACE_DOWN); }
    } else { down_since = 0; face_down = false; }
    if (magnitude > 1.8f && !peak) {
        peaks[peak_index++ % 3] = now;
        if (peak_index >= 3 && now - peaks[peak_index % 3] < 1000000 && now - last_shake >= 1000000) {
            last_shake = now; emit(GESTURE_SHAKE);
        }
    }
    peak = magnitude > 1.8f;
    float delta = magnitude - mean; mean += 0.04f * delta;
    variance = 0.96f * variance + 0.04f * delta * delta;
    if (variance < 0.0025f && s->z > 0.8f) {
        if (!still_since) still_since = now;
        if (!still && now - still_since >= 5000000) { still = true; emit(GESTURE_STILL); }
    } else { still_since = 0; still = false; }
}
