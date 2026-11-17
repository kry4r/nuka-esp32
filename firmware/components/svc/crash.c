// SPDX-License-Identifier: AGPL-3.0-or-later
#include "crash.h"
#include "esp_attr.h"
#include "esp_log.h"
#include "esp_private/panic_internal.h"
#include "esp_system.h"
#include "xtensa_context.h"
#include <stdint.h>

// Panic output uses UART; retain its cause across reset for CDC log export.
static RTC_NOINIT_ATTR struct {
    uint32_t magic, pc, a0, sp, cause, address;
    int core, exception;
    char reason[96], description[128], abort[192];
} record;

static void copy_text(char *target, size_t size, const char *source)
{
    size_t i = 0;
    if (source) while (i + 1 < size && source[i]) { target[i] = source[i]; ++i; }
    target[i] = 0;
}

void __real_esp_panic_handler(panic_info_t *info) __attribute__((noreturn));
void __wrap_esp_panic_handler(panic_info_t *info)
{
    const XtExcFrame *frame = info->frame;
    record.magic = 0;
    record.core = info->core;
    record.exception = g_panic_abort ? PANIC_EXCEPTION_ABORT : info->exception;
    record.pc = frame->pc; record.a0 = frame->a0; record.sp = frame->a1;
    record.cause = frame->exccause; record.address = frame->excvaddr;
    copy_text(record.reason, sizeof(record.reason), info->reason);
    copy_text(record.description, sizeof(record.description), info->description);
    copy_text(record.abort, sizeof(record.abort), g_panic_abort_details);
    record.magic = 0x4e554b41;
    __real_esp_panic_handler(info);
}

void crash_report(void)
{
    esp_reset_reason_t reason = esp_reset_reason();
    if (record.magic == 0x4e554b41 && (reason == ESP_RST_PANIC ||
        reason == ESP_RST_INT_WDT || reason == ESP_RST_TASK_WDT || reason == ESP_RST_WDT)) {
        ESP_LOGE("crash", "core=%d exception=%d pc=%08lx a0=%08lx sp=%08lx cause=%lu address=%08lx",
            record.core, record.exception, (unsigned long)record.pc, (unsigned long)record.a0,
            (unsigned long)record.sp, (unsigned long)record.cause, (unsigned long)record.address);
        ESP_LOGE("crash", "reason=%s description=%s abort=%s",
            record.reason, record.description, record.abort);
    }
    record.magic = 0;
}
