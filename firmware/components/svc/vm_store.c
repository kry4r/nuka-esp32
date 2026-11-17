// SPDX-License-Identifier: AGPL-3.0-or-later
#include "vm_store.h"
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "nuka_events.h"
static struct { cJSON *data; double rev; bool received; } slots[VM_COUNT];
static SemaphoreHandle_t mutex;
void vm_store_init(void) { mutex=xSemaphoreCreateMutex();ESP_ERROR_CHECK(mutex?ESP_OK:ESP_ERR_NO_MEM); }
bool vm_store_replace(vm_topic_t topic, const cJSON *data)
{
    double rev = cJSON_GetObjectItemCaseSensitive(data, "rev")->valuedouble;
    xSemaphoreTake(mutex, portMAX_DELAY);
    bool changed = !slots[topic].received || rev > slots[topic].rev;
    if (changed) {
        cJSON_Delete(slots[topic].data);
        slots[topic].data = cJSON_Duplicate(data, true);
        slots[topic].rev = rev;
        slots[topic].received = true;
    }
    xSemaphoreGive(mutex);
    if (changed) nuka_post(NUKA_VM_CHANGED, &topic, sizeof(topic));
    return changed;
}
cJSON *vm_store_snapshot(vm_topic_t topic)
{
    xSemaphoreTake(mutex, portMAX_DELAY);
    cJSON *copy = slots[topic].data ? cJSON_Duplicate(slots[topic].data, true) : NULL;
    xSemaphoreGive(mutex);
    return copy;
}
void vm_store_new_connection(void)
{
    xSemaphoreTake(mutex, portMAX_DELAY);
    for (int i = 0; i < VM_COUNT; ++i) slots[i].received = false;
    xSemaphoreGive(mutex);
}
