// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <stddef.h>
#include <stdint.h>
void esp_fill_random(void *output, size_t size);
uint32_t esp_random(void);
