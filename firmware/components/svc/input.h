// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "keys.h"
typedef enum {
    KEYEV_SHORT, KEYEV_LONG, KEYEV_DOUBLE, KEYEV_POWER_MENU,
    KEYEV_POWER_OFF, KEYEV_RECOVERY, KEYEV_HOLD, KEYEV_RELEASE,KEYEV_COMBO_HOLD,KEYEV_PRESS
} key_event_kind_t;
typedef struct { key_id_t key; key_event_kind_t kind; uint16_t held_ms; } key_event_t;
void input_start(void);
