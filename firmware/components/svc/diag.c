// SPDX-License-Identifier: AGPL-3.0-or-later
#include "diag.h"
#include "store.h"
#include "net.h"
#include "esp_app_desc.h"
#include "esp_heap_caps.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdlib.h>
cJSON *diag_snapshot(void)
{
    cJSON *d = cJSON_CreateObject();
    cJSON_AddStringToObject(d, "fw", esp_app_get_description()->version);
    cJSON_AddNumberToObject(d, "up_s", esp_timer_get_time() / 1000000);
    cJSON_AddNumberToObject(d, "heap_int", heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
    cJSON_AddNumberToObject(d, "heap_int_min", heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL));
    cJSON_AddNumberToObject(d, "psram_free", heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    UBaseType_t count = uxTaskGetNumberOfTasks();
    TaskStatus_t *tasks = malloc(count * sizeof(TaskStatus_t));
    count = uxTaskGetSystemState(tasks, count, NULL);
    cJSON *items = cJSON_AddArrayToObject(d, "tasks");
    for (unsigned i = 0; i < count; ++i) {
        cJSON *item = cJSON_CreateObject(); cJSON_AddStringToObject(item, "name", tasks[i].pcTaskName);
        cJSON_AddNumberToObject(item, "free_b", tasks[i].usStackHighWaterMark); cJSON_AddItemToArray(items, item);
    }
    free(tasks);
    if (net_connected()) cJSON_AddNumberToObject(d, "rssi", net_rssi());
    const char *reasons[] = {"unknown", "power_on", "external", "software", "panic", "interrupt_watchdog", "task_watchdog", "watchdog", "deep_sleep", "brownout", "sdio", "usb", "jtag", "efuse_error", "power_glitch", "cpu_lockup"};
    cJSON_AddStringToObject(d, "reset_reason", reasons[esp_reset_reason()]); return d;
}
