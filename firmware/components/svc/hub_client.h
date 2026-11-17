// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <stdbool.h>
#include "cJSON.h"
#include "esp_http_client.h"
void hub_client_start(void);
void hub_client_init(void);
bool hub_connected(void);
void hub_send(const char *type, cJSON *data, bool request);
void hub_subscribe_detail(const char *topic, const char *id);
void hub_set_feature(const char *topic, bool enabled);
bool hub_feature_enabled(const char *topic);
void hub_disconnect(void);
void hub_reconfigure(void);
void hub_set_card(const char *card);
void hub_request_state(void);
esp_http_client_handle_t hub_http_client(const char *path);
