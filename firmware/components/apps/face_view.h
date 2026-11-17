// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "app.h"
void face_view_create(lv_obj_t *card);
void face_view_visible(bool visible);
void face_view_apply(void);
void face_view_sync(void);
void face_view_touch(int x,int y);
#ifdef NUKA_SIM
bool face_view_preview(const char *state);
void face_view_preview_stats(unsigned *frames,bool *active);
#endif
