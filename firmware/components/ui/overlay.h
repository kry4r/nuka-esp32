// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "app.h"
#include "input.h"
typedef enum { OVERLAY_NONE, OVERLAY_POWER, OVERLAY_PERMISSION, OVERLAY_CRITICAL,
    OVERLAY_LOW_BATTERY, OVERLAY_TOAST, OVERLAY_QUICK, OVERLAY_NOTIFY,
    OVERLAY_RECOVERY, OVERLAY_PAIR, OVERLAY_VAULT, OVERLAY_OTA } overlay_kind_t;
void overlay_init(void);
void overlay_power(void);
void overlay_quick(void);
void overlay_provisioning(void);
void overlay_notifications(void);
void overlay_recovery(void);
void overlay_toast(const char *text);
void overlay_permission(const cJSON *request);
void overlay_permission_done(const cJSON *result);
void overlay_notification(const cJSON *notification);
void overlay_pair(const cJSON *request);
void overlay_vault(const cJSON *request);
void overlay_usb_closed(bool vault);
bool overlay_permission_pending(void);
void overlay_permission_reopen(void);
void overlay_ota(const cJSON *offer,lv_event_cb_t callback);
bool overlay_key(const key_event_t *key);
void overlay_tick(void);
void overlay_close(void);
overlay_kind_t overlay_current(void);
