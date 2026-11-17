// SPDX-License-Identifier: AGPL-3.0-or-later
#include "nuka_events.h"
#include "freertos/FreeRTOS.h"
ESP_EVENT_DEFINE_BASE(NUKA_EVENTS);
esp_event_loop_handle_t nuka_bus;
esp_err_t nuka_bus_init(void)
{
    const esp_event_loop_args_t args = {
        .queue_size = 32, .task_name = "nuka_bus", .task_priority = 4,
        .task_stack_size = 3072, .task_core_id = 0
    };
    return esp_event_loop_create(&args, &nuka_bus);
}
void nuka_post(int32_t event, const void *data, size_t size)
{
    ESP_ERROR_CHECK(esp_event_post_to(nuka_bus, NUKA_EVENTS, event, data, size, portMAX_DELAY));
}
