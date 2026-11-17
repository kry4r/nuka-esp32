// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "app.h"
#include "theme_tokens.h"
#include "widgets/widgets.h"
void theme_init(void);
lv_obj_t *ui_label(lv_obj_t *parent, const char *text, int size);
lv_obj_t *ui_button(lv_obj_t *parent, const char *text, lv_event_cb_t cb, void *data);
lv_obj_t *ui_button_primary(lv_obj_t *parent, const char *text, lv_event_cb_t cb, void *data);
void ui_column(lv_obj_t *parent);
