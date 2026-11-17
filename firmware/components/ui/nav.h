// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "app.h"
#include "esp_err.h"
void nav_init(void);
void nav_tick(void);
void nav_next(void);
void nav_home(void);
void nav_show(const char *id);
void nav_detail(void);
void nav_back(void);
bool nav_in_detail(void);
bool nav_arranging(void);
const app_def_t *nav_current(void);
lv_group_t *nav_focus_group(void);
void nav_set_state(view_state_t state);
void nav_touch(bool pressed, int x, int y);
void nav_refresh(void);
void nav_reload(void);
void nav_arrange(void);
void nav_restore_focus(void);
void nav_runtime_error(esp_err_t error);
