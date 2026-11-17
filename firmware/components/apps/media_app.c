// SPDX-License-Identifier: AGPL-3.0-or-later
#include "media_app.h"
#include "hub_client.h"
#include "usb.h"
#include "nav.h"
#include <string.h>
extern const app_view_t media_view;
static char selected_id[33];
static bool manual;
const cJSON *media_current(const cJSON *vm)
{
    const cJSON *items = cJSON_GetObjectItemCaseSensitive(vm, "items"), *item;
    if (manual) {
        cJSON_ArrayForEach(item, items)
            if (!strcmp(selected_id, cJSON_GetObjectItemCaseSensitive(item, "src_id")->valuestring)) return item;
        manual = false;
    }
    return cJSON_GetArrayItem(items, 0);
}
void media_page(int direction)
{
    cJSON *vm = vm_store_snapshot(VM_MEDIA);
    const cJSON *items = cJSON_GetObjectItemCaseSensitive(vm, "items"), *current = media_current(vm);
    unsigned count = cJSON_GetArraySize(items);
    if (count) {
        unsigned index = 0;
        while (cJSON_GetArrayItem(items, index) != current) ++index;
        index = direction > 0 ? (index + 1) % count : (index + count - 1) % count;
        strcpy(selected_id, cJSON_GetObjectItemCaseSensitive(cJSON_GetArrayItem(items, index), "src_id")->valuestring);
        manual = true; nav_refresh();
    }
    cJSON_Delete(vm);
}
void media_command(const char *command)
{
    cJSON *vm=vm_store_snapshot(VM_MEDIA);const cJSON *item=media_current(vm);
    if(item){const cJSON *can=cJSON_GetObjectItemCaseSensitive(cJSON_GetObjectItemCaseSensitive(item,"can"),command);
        if(cJSON_IsTrue(can)){cJSON *d=cJSON_CreateObject();cJSON_AddStringToObject(d,"src_id",cJSON_GetObjectItemCaseSensitive(item,"src_id")->valuestring);cJSON_AddStringToObject(d,"cmd",command);hub_send("media.cmd",d,false);}}
    else usb_media_key(!strcmp(command,"toggle")?0xCD:!strcmp(command,"next")?0xB5:0xB6);
    cJSON_Delete(vm);
}
static void action(app_action_t a){if(a==ACT_PRIMARY)media_command("toggle");}
const app_def_t media_app={"media",VM_MEDIA,&media_view,action};
