/*
 * Magnolia OS — IPC Subsystem
 * Purpose:
 *     Signal primitive self-tests covering semantics, blocking, and diagnostics.
 *
 * © 2025 Magnolia Project
 */

#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"

#ifdef CONFIG_MAGNOLIA_IPC_SELFTESTS

#include "esp_log.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "kernel/core/ipc/ipc.h"
#include "kernel/core/ipc/ipc_diag.h"
#include "kernel/core/ipc/tests/ipc_signal_tests.h"
#include "kernel/core/ipc/tests/ipc_channel_tests.h"
#include "kernel/core/ipc/tests/ipc_event_flags_tests.h"
#include "kernel/core/ipc/tests/ipc_shm_tests.h"
#include "kernel/core/sched/m_sched.h"
#include "kernel/core/timer/m_timer.h"

static const char *TAG = "ipc_signal_tests";

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
    SemaphoreHandle_t done;
    volatile ipc_error_t result;
} ipc_signal_worker_ctx_t;

static void ipc_signal_wait_worker(void *arg)
{
    ipc_signal_worker_ctx_t *ctx = arg;
    if (ctx == NULL) {
        return;
    }

    ctx->result = ipc_signal_wait(ctx->handle);
    xSemaphoreGive(ctx->done);
    return;
}

static bool run_test_create_destroy(void)
{
    ipc_handle_t handle = IPC_HANDLE_INVALID;
    if (ipc_signal_create(IPC_SIGNAL_MODE_ONE_SHOT, &handle) != IPC_OK) {
        return false;
    }

    bool ok = (ipc_signal_set(handle) == IPC_OK);
    ok &= (ipc_signal_wait(handle) == IPC_OK);
    ok &= (ipc_signal_destroy(handle) == IPC_OK);
    ok &= (ipc_signal_set(handle) == IPC_ERR_OBJECT_DESTROYED);
    return ok;
}

static bool run_test_one_shot_semantics(void)
{
    ipc_handle_t handle = IPC_HANDLE_INVALID;
    if (ipc_signal_create(IPC_SIGNAL_MODE_ONE_SHOT, &handle) != IPC_OK) {
        return false;
    }

    bool ok = (ipc_signal_set(handle) == IPC_OK);
    ok &= (ipc_signal_try_wait(handle) == IPC_OK);
    ok &= (ipc_signal_try_wait(handle) == IPC_ERR_NOT_READY);
    ok &= (ipc_signal_set(handle) == IPC_OK);
    ok &= (ipc_signal_clear(handle) == IPC_OK);
    ok &= (ipc_signal_try_wait(handle) == IPC_ERR_NOT_READY);
    ipc_signal_destroy(handle);
    return ok;
}

static bool run_test_counting_semantics(void)
{
    ipc_handle_t handle = IPC_HANDLE_INVALID;
    if (ipc_signal_create(IPC_SIGNAL_MODE_COUNTING, &handle) != IPC_OK) {
        return false;
    }

    bool ok = (ipc_signal_set(handle) == IPC_OK);
    ok &= (ipc_signal_set(handle) == IPC_OK);
    ok &= (ipc_signal_try_wait(handle) == IPC_OK);
    ok &= (ipc_signal_try_wait(handle) == IPC_OK);
    ok &= (ipc_signal_try_wait(handle) == IPC_ERR_NOT_READY);
    ipc_signal_destroy(handle);
    return ok;
}

static bool run_test_blocking_wait(void)
{
    ipc_handle_t handle = IPC_HANDLE_INVALID;
    if (ipc_signal_create(IPC_SIGNAL_MODE_ONE_SHOT, &handle) != IPC_OK) {
        return false;
    }

    static StaticSemaphore_t storage;
    SemaphoreHandle_t done = xSemaphoreCreateBinaryStatic(&storage);
    if (done == NULL) {
        ipc_signal_destroy(handle);
        return false;
    }

    ipc_signal_worker_ctx_t ctx = {.handle = handle,
                                   .done = done,
                                   .result = IPC_ERR_SHUTDOWN};

    m_sched_task_id_t worker_id = M_SCHED_TASK_ID_INVALID;
    m_sched_task_options_t opts = {
        .name = "ipc_sig_wait",
        .entry = ipc_signal_wait_worker,
        .argument = &ctx,
        .stack_depth = configMINIMAL_STACK_SIZE,
        .priority = (tskIDLE_PRIORITY + 2),
        .cpu_affinity = M_SCHED_CPU_AFFINITY_ANY,
    };

    if (m_sched_task_create(&opts, &worker_id) != M_SCHED_OK) {
        ipc_signal_destroy(handle);
        return false;
    }

    m_sched_sleep_ms(5);
    ipc_signal_set(handle);

    bool ok = (xSemaphoreTake(done, pdMS_TO_TICKS(500)) == pdTRUE);
    ok &= (ctx.result == IPC_OK);

    ipc_signal_destroy(handle);
    return ok;
}

static bool run_test_timed_wait_timeout(void)
{
    ipc_handle_t handle = IPC_HANDLE_INVALID;
    if (ipc_signal_create(IPC_SIGNAL_MODE_ONE_SHOT, &handle) != IPC_OK) {
        return false;
    }

    ipc_error_t result = ipc_signal_timed_wait(handle, 1000);
    bool ok = (result == IPC_ERR_TIMEOUT);
    ipc_signal_destroy(handle);
    return ok;
}

static bool run_test_non_blocking_not_ready(void)
{
    ipc_handle_t handle = IPC_HANDLE_INVALID;
    if (ipc_signal_create(IPC_SIGNAL_MODE_ONE_SHOT, &handle) != IPC_OK) {
        return false;
    }

    bool ok = (ipc_signal_try_wait(handle) == IPC_ERR_NOT_READY);
    ipc_signal_destroy(handle);
    return ok;
}

static bool run_test_destroy_wakes_waiters(void)
{
    ipc_handle_t handle = IPC_HANDLE_INVALID;
    if (ipc_signal_create(IPC_SIGNAL_MODE_ONE_SHOT, &handle) != IPC_OK) {
        return false;
    }

    static StaticSemaphore_t storage;
    SemaphoreHandle_t done = xSemaphoreCreateBinaryStatic(&storage);
    if (done == NULL) {
        ipc_signal_destroy(handle);
        return false;
    }

    ipc_signal_worker_ctx_t ctx = {.handle = handle,
                                   .done = done,
                                   .result = IPC_ERR_SHUTDOWN};

    m_sched_task_id_t worker_id = M_SCHED_TASK_ID_INVALID;
    m_sched_task_options_t opts = {
        .name = "ipc_sig_wait",
        .entry = ipc_signal_wait_worker,
        .argument = &ctx,
        .stack_depth = configMINIMAL_STACK_SIZE,
        .priority = (tskIDLE_PRIORITY + 2),
        .cpu_affinity = M_SCHED_CPU_AFFINITY_ANY,
    };

    if (m_sched_task_create(&opts, &worker_id) != M_SCHED_OK) {
        ipc_signal_destroy(handle);
        return false;
    }

    m_sched_sleep_ms(5);
    ipc_signal_destroy(handle);

    bool ok = (xSemaphoreTake(done, pdMS_TO_TICKS(500)) == pdTRUE);
    ok &= (ctx.result == IPC_ERR_OBJECT_DESTROYED);
    return ok;
}

static bool run_test_diag_info(void)
{
    ipc_handle_t handle = IPC_HANDLE_INVALID;
    if (ipc_signal_create(IPC_SIGNAL_MODE_ONE_SHOT, &handle) != IPC_OK) {
        return false;
    }

    bool ok = (ipc_signal_set(handle) == IPC_OK);
    ipc_signal_info_t info = {0};
    ok &= (ipc_diag_signal_info(handle, &info) == IPC_OK);
    ok &= (info.sets == 1);
    ok &= (info.waits == 0);
    ok &= (info.ready == true);
    ipc_object_info_t base = {0};
    ok &= (ipc_diag_object_info(handle, &base) == IPC_OK);
    ok &= (base.waiting_tasks == 0);
    ipc_signal_clear(handle);
    ipc_signal_destroy(handle);
    return ok;
}

static bool run_test_invalid_handle(void)
{
    bool ok = true;
    ok &= (ipc_signal_wait(IPC_HANDLE_INVALID) == IPC_ERR_INVALID_HANDLE);
    ok &= (ipc_signal_set(IPC_HANDLE_INVALID) == IPC_ERR_INVALID_HANDLE);
    ok &= (ipc_signal_try_wait(IPC_HANDLE_INVALID) == IPC_ERR_INVALID_HANDLE);
    ok &= (ipc_signal_timed_wait(IPC_HANDLE_INVALID, 1000) == IPC_ERR_INVALID_HANDLE);
    return ok;
}

void ipc_selftests_run(void)
{
    bool overall = true;
    overall &= test_report("signal create/destroy",
                           run_test_create_destroy());
    overall &= test_report("one-shot semantics",
                           run_test_one_shot_semantics());
    overall &= test_report("counting semantics",
                           run_test_counting_semantics());
    overall &= test_report("blocking wait",
                           run_test_blocking_wait());
    overall &= test_report("timed wait timeout",
                           run_test_timed_wait_timeout());
    overall &= test_report("non-blocking not ready",
                           run_test_non_blocking_not_ready());
    overall &= test_report("destroy wakes waiters",
                           run_test_destroy_wakes_waiters());
    overall &= test_report("diag information",
                           run_test_diag_info());
    overall &= test_report("channel self-tests",
                           ipc_channel_tests_run());
    overall &= test_report("event flags self-tests",
                           ipc_event_flags_tests_run());
    overall &= test_report("shm self-tests",
                           ipc_shm_tests_run());
    overall &= test_report("invalid handle",
                           run_test_invalid_handle());

    ESP_LOGI(TAG, "IPC self-tests %s",
             overall ? "PASSED" : "FAILED");
}

#else

#include "kernel/core/ipc/tests/ipc_signal_tests.h"

void ipc_selftests_run(void)
{
}

#endif /* CONFIG_MAGNOLIA_IPC_SELFTESTS */
