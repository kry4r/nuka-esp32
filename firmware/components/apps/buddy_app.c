// SPDX-License-Identifier: AGPL-3.0-or-later
#include "app.h"
#include "hub_client.h"
#include "nav.h"
#include "overlay.h"
extern const app_view_t buddy_view;
unsigned buddy_selected;
static void action(app_action_t a)
{
    if (a == ACT_PRIMARY && overlay_permission_pending()) { overlay_permission_reopen(); return; }
    if (a == ACT_PRIMARY || a == ACT_SECONDARY) { cJSON *vm = vm_store_snapshot(VM_AGENTS); unsigned count = cJSON_GetArraySize(cJSON_GetObjectItemCaseSensitive(vm,"sessions"));
        if (count) buddy_selected = (buddy_selected + 1) % count;
        cJSON_Delete(vm); nav_refresh(); }
    if (a == ACT_BACK) hub_subscribe_detail(NULL,NULL);
}
const app_def_t buddy_app = {"buddy",VM_AGENTS,&buddy_view,action};
