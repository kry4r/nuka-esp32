// SPDX-License-Identifier: AGPL-3.0-or-later
#include "vm_store.h"
#include <string.h>
static const char *names[] = {"hosts","host_detail","servers","server_detail","ai","agents","notify","media","weather","pet"};
const char *vm_topic_name(vm_topic_t topic) { return names[topic]; }
vm_topic_t vm_topic_from_name(const char *name)
{
    for(int i=0;i<VM_COUNT;++i)if(!strcmp(name,names[i]))return i;
    return VM_NONE;
}
