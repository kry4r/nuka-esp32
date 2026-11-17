// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
void audio_open(bool microphone, unsigned volume);
esp_err_t audio_read(int16_t *mono, size_t count);
esp_err_t audio_write(const int16_t *mono, size_t count);
void audio_close(void);
esp_err_t audio_tone(void);
esp_err_t audio_probe_channels(void);
