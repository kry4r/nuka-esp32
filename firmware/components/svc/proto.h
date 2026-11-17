// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include "vm_store.h"
typedef struct { cJSON *root, *data; const char *type; vm_topic_t topic; } proto_frame_t;
bool proto_json_text_valid(const char *json, size_t size);
bool proto_parse(const char *text, size_t length, proto_frame_t *out);
void proto_frame_free(proto_frame_t *frame);
