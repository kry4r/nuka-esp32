// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "cJSON.h"
typedef enum {
    VM_NONE = -1, VM_HOSTS, VM_HOST_DETAIL, VM_SERVERS, VM_SERVER_DETAIL,
    VM_AI, VM_AGENTS, VM_NOTIFY, VM_MEDIA, VM_WEATHER, VM_PET, VM_COUNT
} vm_topic_t;
void vm_store_init(void);
bool vm_store_replace(vm_topic_t topic, const cJSON *data);
cJSON *vm_store_snapshot(vm_topic_t topic);
void vm_store_new_connection(void);
const char *vm_topic_name(vm_topic_t topic);
vm_topic_t vm_topic_from_name(const char *name);
