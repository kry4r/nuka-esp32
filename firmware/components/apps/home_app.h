// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <stdbool.h>
#include <stdint.h>
typedef enum { HOME_VOICE_IDLE, HOME_VOICE_PREPARING, HOME_VOICE_LISTENING,
    HOME_VOICE_THINKING, HOME_VOICE_SPEAKING, HOME_VOICE_ERROR } home_voice_state_t;
void home_toggle_face(void);
bool home_assistant_face(void);
void home_voice_set(home_voice_state_t state, const char *caption, uint8_t level);
home_voice_state_t home_voice_state(void);
const char *home_voice_caption(void);
uint8_t home_voice_level(void);
