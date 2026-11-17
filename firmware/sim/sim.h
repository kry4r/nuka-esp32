// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <stdbool.h>
#include "cJSON.h"
extern bool sim_running;
extern unsigned sim_battery_pct;
void sim_assets(const char *path);
void sim_load_examples(const char *directory, bool minimal);
void sim_dispatch_events(void);
cJSON *sim_example(const char *directory, const char *name);
