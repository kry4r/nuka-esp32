// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
typedef struct { const char *version; } esp_app_desc_t;
static inline const esp_app_desc_t *esp_app_get_description(void)
{ static const esp_app_desc_t descriptor = { "sim" }; return &descriptor; }
