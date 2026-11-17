// SPDX-License-Identifier: AGPL-3.0-or-later
#include "app.h"
#include "hub_client.h"
#include "nav.h"
#include <string.h>
extern const app_view_t ai_view;
static char selected_id[33];
const cJSON *ai_current(const cJSON *vm)
{
    const cJSON *accounts = cJSON_GetObjectItemCaseSensitive(vm, "accounts"), *account;
    cJSON_ArrayForEach(account, accounts)
        if (!strcmp(cJSON_GetObjectItemCaseSensitive(account, "id")->valuestring, selected_id)) return account;
    account = cJSON_GetArrayItem(accounts, 0);
    if (account) strcpy(selected_id, cJSON_GetObjectItemCaseSensitive(account, "id")->valuestring);
    return account;
}
void ai_page(int direction)
{
    cJSON *vm = vm_store_snapshot(VM_AI);
    const cJSON *accounts = cJSON_GetObjectItemCaseSensitive(vm, "accounts"), *current = ai_current(vm);
    unsigned count = cJSON_GetArraySize(accounts);
    if (count) {
        unsigned index = 0;
        while (cJSON_GetArrayItem(accounts, index) != current) ++index;
        index = direction > 0 ? (index + 1) % count : (index + count - 1) % count;
        strcpy(selected_id, cJSON_GetObjectItemCaseSensitive(cJSON_GetArrayItem(accounts, index), "id")->valuestring);
        nav_refresh();
    }
    cJSON_Delete(vm);
}
static void action(app_action_t a)
{
    if (a == ACT_PRIMARY) ai_page(1);
    if (a == ACT_SECONDARY) hub_send("ai.refresh", cJSON_CreateObject(), true);
    if (a == ACT_BACK) hub_subscribe_detail(NULL,NULL);
}
const app_def_t ai_app = {"ai",VM_AI,&ai_view,action};
