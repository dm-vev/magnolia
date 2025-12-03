/**
 * @file kernel/core/timer/tests/m_timer_tests.c
 * @brief Timer subsystem self-tests.
 * @details Verifies monotonic behavior, deadline conversions, and the queue
 *          helpers that future observers may rely on.
 */
#include "sdkconfig.h"

#ifdef CONFIG_MAGNOLIA_TIMER_SELFTESTS

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "kernel/core/timer/m_timer.h"

static const char *TAG = "timer_tests";

static bool test_report(const char *name, bool success)
{
    if (success) {
        ESP_LOGI(TAG, "[PASS] %s", name);
    } else {
        ESP_LOGE(TAG, "[FAIL] %s", name);
    }
    return success;
}

typedef struct {
    size_t count;
    int fired[2];
} timer_queue_test_ctx_t;

typedef struct {
    timer_queue_test_ctx_t *ctx;
    int id;
} timer_queue_event_t;

static void timer_queue_callback(m_timer_queue_entry_t *entry,
                                 void *context)
{
    (void)entry;
    timer_queue_event_t *event = context;
    if (event == NULL || event->ctx == NULL) {
        return;
    }

    timer_queue_test_ctx_t *ctx = event->ctx;
    if (ctx->count >= 2) {
        return;
    }

    ctx->fired[ctx->count++] = event->id;
}

static bool run_test_monotonic_progress(void)
{
    m_timer_time_t before = m_timer_get_monotonic();
    vTaskDelay(pdMS_TO_TICKS(1));
    m_timer_time_t after = m_timer_get_monotonic();
    return (after >= before);
}

static bool run_test_deadline_ticks(void)
{
    m_timer_deadline_t deadline = m_timer_deadline_from_relative(5000ULL);
    TickType_t ticks = m_timer_deadline_to_ticks(&deadline);
    return (ticks > 0);
}

static bool run_test_queue_ordering(void)
{
    timer_queue_test_ctx_t ctx = {0};
    timer_queue_event_t events[2] = {
        {.ctx = &ctx, .id = 1},
        {.ctx = &ctx, .id = 2},
    };

    m_timer_queue_schedule(m_timer_deadline_from_relative(0),
                           timer_queue_callback,
                           &events[0]);
    m_timer_queue_schedule(m_timer_deadline_from_relative(1500),
                           timer_queue_callback,
                           &events[1]);

    m_timer_queue_process(m_timer_get_monotonic());
    m_timer_queue_process(m_timer_get_monotonic() + 2000ULL);

    bool order_ok = (ctx.count == 2 && ctx.fired[0] == 1
                     && ctx.fired[1] == 2);
    bool empty = (m_timer_queue_length() == 0);
    return order_ok && empty;
}

static bool run_test_queue_cancel(void)
{
    timer_queue_event_t event = {.ctx = NULL, .id = 3};
    m_timer_queue_entry_t *entry =
            m_timer_queue_schedule(m_timer_deadline_from_relative(5000000ULL),
                                   timer_queue_callback,
                                   &event);
    if (entry == NULL) {
        return false;
    }

    bool cancelled = m_timer_queue_cancel(entry);
    return cancelled && (m_timer_queue_length() == 0);
}

void m_timer_selftests_run(void)
{
    bool overall = true;
    overall &= test_report("monotonic progression", run_test_monotonic_progress());
    overall &= test_report("deadline tick conversion", run_test_deadline_ticks());
    overall &= test_report("queue ordering", run_test_queue_ordering());
    overall &= test_report("queue cancel", run_test_queue_cancel());
    ESP_LOGI(TAG, "timer self-tests %s",
             overall ? "PASSED" : "FAILED");
}

#else

#include "kernel/core/timer/tests/m_timer_tests.h"

#endif /* CONFIG_MAGNOLIA_TIMER_SELFTESTS */
