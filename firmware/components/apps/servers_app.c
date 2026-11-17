// SPDX-License-Identifier: AGPL-3.0-or-later
#include "app.h"
#include "hub_client.h"
#include "nav.h"
extern const app_view_t servers_view;
unsigned servers_selected;
static void action(app_action_t a)
{
    if (a == ACT_PRIMARY || a == ACT_SECONDARY) { cJSON *vm = vm_store_snapshot(VM_SERVERS); unsigned count = cJSON_GetArraySize(cJSON_GetObjectItemCaseSensitive(vm,"items"));
        if (count) servers_selected = (servers_selected + 1) % count;
        cJSON_Delete(vm); }
    if (a == ACT_OPEN) { cJSON *vm = vm_store_snapshot(VM_SERVERS); const cJSON *item = cJSON_GetArrayItem(cJSON_GetObjectItemCaseSensitive(vm,"items"),servers_selected);
        if (item) hub_subscribe_detail("servers",cJSON_GetObjectItemCaseSensitive(item,"id")->valuestring);
        cJSON_Delete(vm); }
    if (a == ACT_BACK) hub_subscribe_detail(NULL,NULL);
}
const app_def_t servers_app = {"servers",VM_SERVERS,&servers_view,action};
