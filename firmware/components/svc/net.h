// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <stdbool.h>
#include "esp_err.h"
void net_start(void);
esp_err_t net_poll(void);
void net_reload(void);
void net_start_provisioning(void);
bool net_connected(void);
int net_rssi(void);
const char *net_pair_code(void);
const char *net_provision_ssid(void);
const char *net_provision_url(void);
