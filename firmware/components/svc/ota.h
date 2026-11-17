// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <stdbool.h>
#include "cJSON.h"
void ota_start(void);
void ota_offer(const cJSON *data);
void ota_accept(bool accept);
void ota_hub_ready(void);
bool ota_pending(void);
unsigned ota_progress(void);
