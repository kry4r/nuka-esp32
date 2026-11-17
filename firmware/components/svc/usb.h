// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "cJSON.h"
void usb_start(void);
void usb_send(const char *type, int request_id, const cJSON *data);
void usb_pair_decide(bool accept);
void usb_vault_decide(bool accept);
bool usb_enter_admin(void);
void usb_request_provisioning(void);
void usb_restart_download(void);
bool usb_attached(void);
bool usb_admin_active(void);
void usb_type_text(const char *text);
void usb_keyboard_report(uint8_t modifiers, const uint8_t codes[6]);
void usb_media_key(uint16_t code);
