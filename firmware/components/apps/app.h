// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "lvgl.h"
#include "vm_store.h"
typedef enum { VIEW_NORMAL, VIEW_LOADING, VIEW_OFFLINE, VIEW_UNCONFIGURED } view_state_t;
typedef enum { ACT_PRIMARY, ACT_SECONDARY, ACT_OPEN, ACT_BACK } app_action_t;
typedef struct {
    void (*create)(lv_obj_t *card);
    void (*apply)(const void *vm, view_state_t st);
    void (*open_detail)(lv_obj_t *page);
} app_view_t;
typedef struct {
    const char *id;
    vm_topic_t topic;
    const app_view_t *view;
    void (*on_action)(app_action_t a);
} app_def_t;
extern const app_def_t *nuka_apps[];
extern const unsigned nuka_app_count;
