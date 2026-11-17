// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "qmi8658.h"
typedef enum { GESTURE_RAISE, GESTURE_FACE_DOWN, GESTURE_SHAKE, GESTURE_STILL, GESTURE_POMO_DONE } gesture_t;
void imu_gesture_sample(const imu_sample_t *sample);
