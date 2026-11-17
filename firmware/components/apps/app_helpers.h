// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "theme.h"
#include "i18n.h"
#include "nav.h"
// Optional VM fields: NULL / false when the Hub omitted them (PROTOCOL §3.4 omits instead of sending zero).
const char *app_str(const cJSON *object, const char *key);
bool app_num(const cJSON *object, const char *key, double *value);
// A filled view root that spans the card; children stay non-clickable so a tap falls through to the card.
lv_obj_t *app_root(lv_obj_t *card);
// Value over a dim caption; returns the value label, whose parent is the whole stat.
lv_obj_t *app_stat(lv_obj_t *parent, int size, const char *caption);
void app_show(lv_obj_t *object, bool visible);
void app_countdown(char *text, size_t size, double epoch);
void app_detail_rows(lv_obj_t *parent, vm_topic_t topic);
