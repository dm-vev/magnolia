/*
 * Magnolia OS — IPC Subsystem
 * Purpose:
 *     Event flags primitive self-tests covering semantics, blocking, and diagnostics.
 *
 * © 2025 Magnolia Project
 */

#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"

#ifdef CONFIG_MAGNOLIA_IPC_SELFTESTS

#include "esp_log.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "kernel/core/ipc/ipc_diag.h"
#include "kernel/core/ipc/ipc_event_flags.h"
#include "kernel/core/ipc/tests/ipc_event_flags_tests.h"
#include "kernel/core/sched/m_sched.h"
#include "kernel/core/timer/m_timer.h"

static const char *TAG = "ipc_event_flags_tests";

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
    ipc_handle_t handle;
    ipc_event_flags_wait_type_t wait_type;
    uint32_t mask;
    uint64_t timeout_us;
    QueueHandle_t order_queue;
    int id;
    SemaphoreHandle_t done;
    volatile ipc_error_t result;
} ipc_event_flags_worker_ctx_t;

static void ipc_event_flags_wait_worker(void *arg)
{
    ipc_event_flags_worker_ctx_t *ctx = arg;
    if (ctx == NULL) {
        return;
    }

    if (ctx->timeout_us == M_TIMER_TIMEOUT_FOREVER) {
        ctx->result = ipc_event_flags_wait(ctx->handle,
                                           ctx->wait_type,
                                           ctx->mask);
    } else {
        ctx->result = ipc_event_flags_timed_wait(ctx->handle,
                                                 ctx->wait_type,
                                                 ctx->mask,
                                                 ctx->timeout_us);
    }

    if (ctx->order_queue != NULL) {
        xQueueSend(ctx->order_queue, &ctx->id, portMAX_DELAY);
    }

    if (ctx->done != NULL) {
        xSemaphoreGive(ctx->done);
    }
}

static bool run_test_create_destroy(void)
{
    ipc_handle_t handle = IPC_HANDLE_INVALID;
    if (ipc_event_flags_create(IPC_EVENT_FLAGS_MODE_AUTO_CLEAR,
                               IPC_EVENT_FLAGS_MASK_MODE_SUPERSET,
                               &handle) != IPC_OK) {
        return false;
    }

    bool ok = true;
    ok &= (ipc_event_flags_set(handle, 0x1) == IPC_OK);
    uint32_t mask = 0;
    ok &= (ipc_event_flags_read(handle, &mask) == IPC_OK);
    ok &= (mask == 0x1);
    ok &= (ipc_event_flags_destroy(handle) == IPC_OK);
    ok &= (ipc_event_flags_set(handle, 0x1) == IPC_ERR_OBJECT_DESTROYED);
    return ok;
}

static bool run_test_set_clear_read(void)
{
    ipc_handle_t handle = IPC_HANDLE_INVALID;
    if (ipc_event_flags_create(IPC_EVENT_FLAGS_MODE_MANUAL_CLEAR,
                               IPC_EVENT_FLAGS_MASK_MODE_EXACT,
                               &handle) != IPC_OK) {
        return false;
    }

    bool ok = true;
    ok &= (ipc_event_flags_set(handle, 0xA) == IPC_OK);
    uint32_t mask = 0;
    ok &= (ipc_event_flags_read(handle, &mask) == IPC_OK);
    ok &= (mask == 0xA);
    ok &= (ipc_event_flags_clear(handle, 0x8) == IPC_OK);
    ok &= (ipc_event_flags_read(handle, &mask) == IPC_OK);
    ok &= (mask == 0x2);
    ok &= (ipc_event_flags_destroy(handle) == IPC_OK);
    return ok;
}

static bool run_test_auto_manual_modes(void)
{
    bool ok = true;
    ipc_handle_t handle = IPC_HANDLE_INVALID;
    if (ipc_event_flags_create(IPC_EVENT_FLAGS_MODE_AUTO_CLEAR,
                               IPC_EVENT_FLAGS_MASK_MODE_SUPERSET,
                               &handle) != IPC_OK) {
        return false;
    }

    ok &= (ipc_event_flags_set(handle, 0x3) == IPC_OK);
    ok &= (ipc_event_flags_wait(handle, IPC_EVENT_FLAGS_WAIT_ALL, 0x3)
           == IPC_OK);
    uint32_t mask = 0;
    ok &= (ipc_event_flags_read(handle, &mask) == IPC_OK);
    ok &= (mask == 0);
    ok &= (ipc_event_flags_destroy(handle) == IPC_OK);

    handle = IPC_HANDLE_INVALID;
    if (ipc_event_flags_create(IPC_EVENT_FLAGS_MODE_MANUAL_CLEAR,
                               IPC_EVENT_FLAGS_MASK_MODE_SUPERSET,
                               &handle) != IPC_OK) {
        return false;
    }

    ok &= (ipc_event_flags_set(handle, 0x3) == IPC_OK);
    ok &= (ipc_event_flags_wait(handle, IPC_EVENT_FLAGS_WAIT_ALL, 0x3)
           == IPC_OK);
    mask = 0;
    ok &= (ipc_event_flags_read(handle, &mask) == IPC_OK);
    ok &= (mask == 0x3);
    ok &= (ipc_event_flags_clear(handle, 0x3) == IPC_OK);
    ok &= (ipc_event_flags_read(handle, &mask) == IPC_OK);
    ok &= (mask == 0);
    ok &= (ipc_event_flags_destroy(handle) == IPC_OK);
    return ok;
}

static bool run_test_wait_variants(void)
{
    bool ok = true;
    ipc_handle_t handle = IPC_HANDLE_INVALID;
    if (ipc_event_flags_create(IPC_EVENT_FLAGS_MODE_MANUAL_CLEAR,
                               IPC_EVENT_FLAGS_MASK_MODE_EXACT,
                               &handle) != IPC_OK) {
        return false;
    }

    ok &= (ipc_event_flags_set(handle, 0x5) == IPC_OK);
    ok &= (ipc_event_flags_try_wait(handle, IPC_EVENT_FLAGS_WAIT_ANY, 0x1)
           == IPC_OK);
    ok &= (ipc_event_flags_try_wait(handle, IPC_EVENT_FLAGS_WAIT_ANY, 0x8)
           == IPC_ERR_NOT_READY);
    ok &= (ipc_event_flags_try_wait(handle, IPC_EVENT_FLAGS_WAIT_ALL, 0x5)
           == IPC_OK);
    ok &= (ipc_event_flags_try_wait(handle, IPC_EVENT_FLAGS_WAIT_ALL, 0x7)
           == IPC_ERR_NOT_READY);
    ok &= (ipc_event_flags_clear(handle, 0x5) == IPC_OK);
    ok &= (ipc_event_flags_set(handle, 0x3) == IPC_OK);
    ok &= (ipc_event_flags_try_wait(handle, IPC_EVENT_FLAGS_WAIT_MASK, 0x3)
           == IPC_OK);
    ok &= (ipc_event_flags_try_wait(handle, IPC_EVENT_FLAGS_WAIT_MASK, 0x7)
           == IPC_ERR_NOT_READY);
    ok &= (ipc_event_flags_destroy(handle) == IPC_OK);

    handle = IPC_HANDLE_INVALID;
    if (ipc_event_flags_create(IPC_EVENT_FLAGS_MODE_MANUAL_CLEAR,
                               IPC_EVENT_FLAGS_MASK_MODE_SUPERSET,
                               &handle) != IPC_OK) {
        return false;
    }

    ok &= (ipc_event_flags_set(handle, 0x7) == IPC_OK);
    ok &= (ipc_event_flags_try_wait(handle, IPC_EVENT_FLAGS_WAIT_MASK, 0x3)
           == IPC_OK);
    ok &= (ipc_event_flags_try_wait(handle, IPC_EVENT_FLAGS_WAIT_MASK, 0x9)
           == IPC_ERR_NOT_READY);
    ok &= (ipc_event_flags_destroy(handle) == IPC_OK);
    return ok;
}

static bool run_test_multiple_waiters_order(void)
{
    bool ok = true;
    ipc_handle_t handle = IPC_HANDLE_INVALID;
    if (ipc_event_flags_create(IPC_EVENT_FLAGS_MODE_MANUAL_CLEAR,
                               IPC_EVENT_FLAGS_MASK_MODE_SUPERSET,
                               &handle) != IPC_OK) {
        return false;
    }

    StaticSemaphore_t storage_a;
    StaticSemaphore_t storage_b;
    SemaphoreHandle_t done_a = xSemaphoreCreateBinaryStatic(&storage_a);
    SemaphoreHandle_t done_b = xSemaphoreCreateBinaryStatic(&storage_b);
    if (done_a == NULL || done_b == NULL) {
        ipc_event_flags_destroy(handle);
        return false;
    }

    QueueHandle_t order_queue = xQueueCreate(2, sizeof(int));
    if (order_queue == NULL) {
        ipc_event_flags_destroy(handle);
        return false;
    }

    ipc_event_flags_worker_ctx_t ctx[2] = {
        {.handle = handle,
         .wait_type = IPC_EVENT_FLAGS_WAIT_ALL,
         .mask = 0x1,
         .timeout_us = M_TIMER_TIMEOUT_FOREVER,
         .order_queue = order_queue,
         .id = 1,
         .done = done_a,
         .result = IPC_ERR_SHUTDOWN},
        {.handle = handle,
         .wait_type = IPC_EVENT_FLAGS_WAIT_ALL,
         .mask = 0x1,
         .timeout_us = M_TIMER_TIMEOUT_FOREVER,
         .order_queue = order_queue,
         .id = 2,
         .done = done_b,
         .result = IPC_ERR_SHUTDOWN},
    };

    m_sched_task_id_t worker_ids[2];
    for (int i = 0; i < 2; i++) {
        m_sched_task_options_t opts = {
            .name = (i == 0) ? "ipc_evt_wait_a" : "ipc_evt_wait_b",
            .entry = ipc_event_flags_wait_worker,
            .argument = &ctx[i],
            .stack_depth = configMINIMAL_STACK_SIZE,
            .priority = (tskIDLE_PRIORITY + 2),
            .cpu_affinity = M_SCHED_CPU_AFFINITY_ANY,
        };
        if (m_sched_task_create(&opts, &worker_ids[i]) != M_SCHED_OK) {
            ipc_event_flags_destroy(handle);
            vQueueDelete(order_queue);
            return false;
        }
    }

    m_sched_sleep_ms(5);
    ok &= (ipc_event_flags_set(handle, 0x1) == IPC_OK);
    ok &= (xSemaphoreTake(done_a, pdMS_TO_TICKS(500)) == pdTRUE);
    ok &= (xSemaphoreTake(done_b, pdMS_TO_TICKS(500)) == pdTRUE);
    ok &= (ctx[0].result == IPC_OK);
    ok &= (ctx[1].result == IPC_OK);

    int order[2] = {0};
    ok &= (xQueueReceive(order_queue, &order[0], pdMS_TO_TICKS(500)) == pdTRUE);
    ok &= (xQueueReceive(order_queue, &order[1], pdMS_TO_TICKS(500)) == pdTRUE);
    ok &= (order[0] == 1);
    ok &= (order[1] == 2);

    vQueueDelete(order_queue);
    ipc_event_flags_destroy(handle);
    return ok;
}

static bool run_test_clear_no_wake(void)
{
    ipc_handle_t handle = IPC_HANDLE_INVALID;
    if (ipc_event_flags_create(IPC_EVENT_FLAGS_MODE_MANUAL_CLEAR,
                               IPC_EVENT_FLAGS_MASK_MODE_SUPERSET,
                               &handle) != IPC_OK) {
        return false;
    }

    bool ok = true;
    ok &= (ipc_event_flags_set(handle, 0x2) == IPC_OK);

    StaticSemaphore_t storage;
    SemaphoreHandle_t done = xSemaphoreCreateBinaryStatic(&storage);
    if (done == NULL) {
        ipc_event_flags_destroy(handle);
        return false;
    }

    ipc_event_flags_worker_ctx_t ctx = {
        .handle = handle,
        .wait_type = IPC_EVENT_FLAGS_WAIT_ALL,
        .mask = 0x1,
        .timeout_us = 100000,
        .done = done,
        .result = IPC_ERR_SHUTDOWN,
    };

    m_sched_task_id_t worker_id = M_SCHED_TASK_ID_INVALID;
    m_sched_task_options_t opts = {
        .name = "ipc_evt_clear",
        .entry = ipc_event_flags_wait_worker,
        .argument = &ctx,
        .stack_depth = configMINIMAL_STACK_SIZE,
        .priority = (tskIDLE_PRIORITY + 2),
        .cpu_affinity = M_SCHED_CPU_AFFINITY_ANY,
    };

    if (m_sched_task_create(&opts, &worker_id) != M_SCHED_OK) {
        ipc_event_flags_destroy(handle);
        return false;
    }

    m_sched_sleep_ms(5);
    ok &= (ipc_event_flags_clear(handle, 0x2) == IPC_OK);
    ok &= (xSemaphoreTake(done, pdMS_TO_TICKS(500)) == pdTRUE);
    ok &= (ctx.result == IPC_ERR_TIMEOUT);

    ipc_event_flags_destroy(handle);
    return ok;
}

static bool run_test_ready_on_entry(void)
{
    ipc_handle_t handle = IPC_HANDLE_INVALID;
    if (ipc_event_flags_create(IPC_EVENT_FLAGS_MODE_AUTO_CLEAR,
                               IPC_EVENT_FLAGS_MASK_MODE_SUPERSET,
                               &handle) != IPC_OK) {
        return false;
    }

    bool ok = true;
    ok &= (ipc_event_flags_set(handle, 0x1) == IPC_OK);
    ok &= (ipc_event_flags_wait(handle, IPC_EVENT_FLAGS_WAIT_ANY, 0x1)
           == IPC_OK);
    uint32_t mask = 0;
    ok &= (ipc_event_flags_read(handle, &mask) == IPC_OK);
    ok &= (mask == 0);
    ipc_event_flags_destroy(handle);
    return ok;
}

static bool run_test_blocking_wait(void)
{
    ipc_handle_t handle = IPC_HANDLE_INVALID;
    if (ipc_event_flags_create(IPC_EVENT_FLAGS_MODE_MANUAL_CLEAR,
                               IPC_EVENT_FLAGS_MASK_MODE_SUPERSET,
                               &handle) != IPC_OK) {
        return false;
    }

    StaticSemaphore_t storage;
    SemaphoreHandle_t done = xSemaphoreCreateBinaryStatic(&storage);
    if (done == NULL) {
        ipc_event_flags_destroy(handle);
        return false;
    }

    ipc_event_flags_worker_ctx_t ctx = {
        .handle = handle,
        .wait_type = IPC_EVENT_FLAGS_WAIT_ALL,
        .mask = 0x4,
        .timeout_us = M_TIMER_TIMEOUT_FOREVER,
        .done = done,
        .result = IPC_ERR_SHUTDOWN,
    };

    m_sched_task_id_t worker_id = M_SCHED_TASK_ID_INVALID;
    m_sched_task_options_t opts = {
        .name = "ipc_evt_block",
        .entry = ipc_event_flags_wait_worker,
        .argument = &ctx,
        .stack_depth = configMINIMAL_STACK_SIZE,
        .priority = (tskIDLE_PRIORITY + 2),
        .cpu_affinity = M_SCHED_CPU_AFFINITY_ANY,
    };

    if (m_sched_task_create(&opts, &worker_id) != M_SCHED_OK) {
        ipc_event_flags_destroy(handle);
        return false;
    }

    m_sched_sleep_ms(5);
    bool ok = (ipc_event_flags_set(handle, 0x4) == IPC_OK);
    ok &= (xSemaphoreTake(done, pdMS_TO_TICKS(500)) == pdTRUE);
    ok &= (ctx.result == IPC_OK);
    ipc_event_flags_destroy(handle);
    return ok;
}

static bool run_test_timed_wait_timeout(void)
{
    ipc_handle_t handle = IPC_HANDLE_INVALID;
    if (ipc_event_flags_create(IPC_EVENT_FLAGS_MODE_MANUAL_CLEAR,
                               IPC_EVENT_FLAGS_MASK_MODE_EXACT,
                               &handle) != IPC_OK) {
        return false;
    }

    ipc_error_t result = ipc_event_flags_timed_wait(handle,
                                                   IPC_EVENT_FLAGS_WAIT_ALL,
                                                   0x1,
                                                   1000);
    bool ok = (result == IPC_ERR_TIMEOUT);
    ipc_event_flags_destroy(handle);
    return ok;
}

static bool run_test_timed_wait_ready(void)
{
    ipc_handle_t handle = IPC_HANDLE_INVALID;
    if (ipc_event_flags_create(IPC_EVENT_FLAGS_MODE_MANUAL_CLEAR,
                               IPC_EVENT_FLAGS_MASK_MODE_SUPERSET,
                               &handle) != IPC_OK) {
        return false;
    }

    StaticSemaphore_t storage;
    SemaphoreHandle_t done = xSemaphoreCreateBinaryStatic(&storage);
    if (done == NULL) {
        ipc_event_flags_destroy(handle);
        return false;
    }

    ipc_event_flags_worker_ctx_t ctx = {
        .handle = handle,
        .wait_type = IPC_EVENT_FLAGS_WAIT_ALL,
        .mask = 0x1,
        .timeout_us = 100000,
        .done = done,
        .result = IPC_ERR_SHUTDOWN,
    };

    m_sched_task_id_t worker_id = M_SCHED_TASK_ID_INVALID;
    m_sched_task_options_t opts = {
        .name = "ipc_evt_timed",
        .entry = ipc_event_flags_wait_worker,
        .argument = &ctx,
        .stack_depth = configMINIMAL_STACK_SIZE,
        .priority = (tskIDLE_PRIORITY + 2),
        .cpu_affinity = M_SCHED_CPU_AFFINITY_ANY,
    };

    if (m_sched_task_create(&opts, &worker_id) != M_SCHED_OK) {
        ipc_event_flags_destroy(handle);
        return false;
    }

    m_sched_sleep_ms(5);
    bool ok = (ipc_event_flags_set(handle, 0x1) == IPC_OK);
    ok &= (xSemaphoreTake(done, pdMS_TO_TICKS(500)) == pdTRUE);
    ok &= (ctx.result == IPC_OK);
    ipc_event_flags_destroy(handle);
    return ok;
}

static bool run_test_destroy_wakes_waiters(void)
{
    ipc_handle_t handle = IPC_HANDLE_INVALID;
    if (ipc_event_flags_create(IPC_EVENT_FLAGS_MODE_MANUAL_CLEAR,
                               IPC_EVENT_FLAGS_MASK_MODE_SUPERSET,
                               &handle) != IPC_OK) {
        return false;
    }

    StaticSemaphore_t storage;
    SemaphoreHandle_t done = xSemaphoreCreateBinaryStatic(&storage);
    if (done == NULL) {
        ipc_event_flags_destroy(handle);
        return false;
    }

    ipc_event_flags_worker_ctx_t ctx = {
        .handle = handle,
        .wait_type = IPC_EVENT_FLAGS_WAIT_ALL,
        .mask = 0x1,
        .timeout_us = M_TIMER_TIMEOUT_FOREVER,
        .done = done,
        .result = IPC_ERR_SHUTDOWN,
    };

    m_sched_task_id_t worker_id = M_SCHED_TASK_ID_INVALID;
    m_sched_task_options_t opts = {
        .name = "ipc_evt_dest",
        .entry = ipc_event_flags_wait_worker,
        .argument = &ctx,
        .stack_depth = configMINIMAL_STACK_SIZE,
        .priority = (tskIDLE_PRIORITY + 2),
        .cpu_affinity = M_SCHED_CPU_AFFINITY_ANY,
    };

    if (m_sched_task_create(&opts, &worker_id) != M_SCHED_OK) {
        ipc_event_flags_destroy(handle);
        return false;
    }

    m_sched_sleep_ms(5);
    ipc_event_flags_destroy(handle);
    bool ok = (xSemaphoreTake(done, pdMS_TO_TICKS(500)) == pdTRUE);
    ok &= (ctx.result == IPC_ERR_OBJECT_DESTROYED);
    return ok;
}

static bool run_test_destroy_before_wait(void)
{
    ipc_handle_t handle = IPC_HANDLE_INVALID;
    if (ipc_event_flags_create(IPC_EVENT_FLAGS_MODE_MANUAL_CLEAR,
                               IPC_EVENT_FLAGS_MASK_MODE_EXACT,
                               &handle) != IPC_OK) {
        return false;
    }

    ipc_event_flags_destroy(handle);
    return (ipc_event_flags_wait(handle, IPC_EVENT_FLAGS_WAIT_ANY, 0x1)
            == IPC_ERR_OBJECT_DESTROYED);
}

static bool run_test_invalid_handle(void)
{
    bool ok = true;
    ok &= (ipc_event_flags_set(IPC_HANDLE_INVALID, 0x1)
           == IPC_ERR_INVALID_HANDLE);
    ok &= (ipc_event_flags_clear(IPC_HANDLE_INVALID, 0x1)
           == IPC_ERR_INVALID_HANDLE);
    uint32_t mask = 0;
    ok &= (ipc_event_flags_read(IPC_HANDLE_INVALID, &mask)
           == IPC_ERR_INVALID_HANDLE);
    ok &= (ipc_event_flags_try_wait(IPC_HANDLE_INVALID,
                                    IPC_EVENT_FLAGS_WAIT_ANY,
                                    0x1)
           == IPC_ERR_INVALID_HANDLE);
    ok &= (ipc_event_flags_wait(IPC_HANDLE_INVALID,
                                IPC_EVENT_FLAGS_WAIT_ANY,
                                0x1)
           == IPC_ERR_INVALID_HANDLE);
    ok &= (ipc_event_flags_timed_wait(IPC_HANDLE_INVALID,
                                      IPC_EVENT_FLAGS_WAIT_ANY,
                                      0x1,
                                      1000)
           == IPC_ERR_INVALID_HANDLE);
    ok &= (ipc_event_flags_destroy(IPC_HANDLE_INVALID) == IPC_ERR_INVALID_HANDLE);
    return ok;
}

static bool run_test_not_ready_try_wait(void)
{
    ipc_handle_t handle = IPC_HANDLE_INVALID;
    if (ipc_event_flags_create(IPC_EVENT_FLAGS_MODE_MANUAL_CLEAR,
                               IPC_EVENT_FLAGS_MASK_MODE_EXACT,
                               &handle) != IPC_OK) {
        return false;
    }

    bool ok = (ipc_event_flags_try_wait(handle,
                                        IPC_EVENT_FLAGS_WAIT_ALL,
                                        0x1)
               == IPC_ERR_NOT_READY);
    ipc_event_flags_destroy(handle);
    return ok;
}

static bool run_test_diag_info(void)
{
    ipc_handle_t handle = IPC_HANDLE_INVALID;
    if (ipc_event_flags_create(IPC_EVENT_FLAGS_MODE_AUTO_CLEAR,
                               IPC_EVENT_FLAGS_MASK_MODE_SUPERSET,
                               &handle) != IPC_OK) {
        return false;
    }

    bool ok = (ipc_event_flags_set(handle, 0x3) == IPC_OK);
    ipc_event_flags_info_t info = {0};
    ok &= (ipc_diag_event_flags_info(handle, 0x1, &info) == IPC_OK);
    ok &= (info.mask == 0x3);
    ok &= (info.mode == IPC_EVENT_FLAGS_MODE_AUTO_CLEAR);
    ok &= (info.mask_mode == IPC_EVENT_FLAGS_MASK_MODE_SUPERSET);
    ok &= (info.waiting_tasks == 0);
    ok &= (info.destroyed == false);
    ok &= (info.ready == true);
    ok &= (info.ready_for_mask == true);
    ok &= (info.metadata_consistent == true);
    ok &= (info.sets == 1);
    ok &= (info.waits == 0);
    ok &= (info.timeouts == 0);
    ipc_event_flags_destroy(handle);
    return ok;
}

bool ipc_event_flags_tests_run(void)
{
    bool overall = true;
    overall &= test_report("event flags create/destroy",
                           run_test_create_destroy());
    overall &= test_report("logging set/clear/read behavior",
                           run_test_set_clear_read());
    overall &= test_report("auto/manual semantics",
                           run_test_auto_manual_modes());
    overall &= test_report("wait variants",
                           run_test_wait_variants());
    overall &= test_report("multiple waiters ordering",
                           run_test_multiple_waiters_order());
    overall &= test_report("clear does not wake",
                           run_test_clear_no_wake());
    overall &= test_report("ready-on-entry",
                           run_test_ready_on_entry());
    overall &= test_report("blocking wait",
                           run_test_blocking_wait());
    overall &= test_report("timed wait timeout",
                           run_test_timed_wait_timeout());
    overall &= test_report("timed wait success",
                           run_test_timed_wait_ready());
    overall &= test_report("destroy wakes waiters",
                           run_test_destroy_wakes_waiters());
    overall &= test_report("destroy before wait",
                           run_test_destroy_before_wait());
    overall &= test_report("invalid handles",
                           run_test_invalid_handle());
    overall &= test_report("non-blocking not ready",
                           run_test_not_ready_try_wait());
    overall &= test_report("diag information",
                           run_test_diag_info());

    ESP_LOGI(TAG, "Event flags self-tests %s",
             overall ? "PASSED" : "FAILED");
    return overall;
}

#else

#include "kernel/core/ipc/tests/ipc_event_flags_tests.h"

bool ipc_event_flags_tests_run(void)
{
    return true;
}

#endif /* CONFIG_MAGNOLIA_IPC_SELFTESTS */
