/*
 * Magnolia OS — IPC Subsystem
 * Purpose:
 *     Channel validation self-tests covering blocking, timed, and destruction flows.
 *
 * © 2025 Magnolia Project
 */

#include "sdkconfig.h"

#include "freertos/FreeRTOS.h"

#if CONFIG_MAGNOLIA_IPC_ENABLED && CONFIG_MAGNOLIA_IPC_SELFTESTS

#include "esp_log.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include <string.h>

#include "kernel/core/ipc/ipc_channel.h"
#include "kernel/core/ipc/ipc_diag.h"
#include "kernel/core/ipc/tests/ipc_channel_tests.h"
#include "kernel/core/sched/m_sched.h"

static const char *TAG = "ipc_channel_tests";

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
    size_t length;
    char message[IPC_CHANNEL_MAX_MESSAGE_SIZE];
} ipc_channel_send_worker_ctx_t;

static void ipc_channel_send_worker(void *arg)
{
    ipc_channel_send_worker_ctx_t *ctx = arg;
    if (ctx == NULL || ctx->done == NULL) {
        return;
    }

    ctx->result = m_ipc_channel_send(ctx->handle, ctx->message, ctx->length);
    xSemaphoreGive(ctx->done);
}

typedef struct {
    ipc_handle_t handle;
    SemaphoreHandle_t done;
    volatile ipc_error_t result;
    size_t received_length;
    char buffer[IPC_CHANNEL_MAX_MESSAGE_SIZE];
} ipc_channel_recv_worker_ctx_t;

static void ipc_channel_recv_worker(void *arg)
{
    ipc_channel_recv_worker_ctx_t *ctx = arg;
    if (ctx == NULL || ctx->done == NULL) {
        return;
    }

    size_t received = 0;
    ctx->result = m_ipc_channel_recv(ctx->handle,
                                   ctx->buffer,
                                   sizeof(ctx->buffer),
                                   &received);
    ctx->received_length = received;
    xSemaphoreGive(ctx->done);
}

static bool run_test_create_destroy(void)
{
    ipc_handle_t handle = IPC_HANDLE_INVALID;
    if (m_ipc_channel_create(1, 8, &handle) != IPC_OK) {
        return false;
    }

    bool ok = (m_ipc_channel_destroy(handle) == IPC_OK);
    char buffer[2] = {0};
    size_t received = 0;
    ok &= (m_ipc_channel_send(handle, "x", 1) == IPC_ERR_OBJECT_DESTROYED);
    ok &= (m_ipc_channel_recv(handle, buffer, sizeof(buffer), &received)
           == IPC_ERR_OBJECT_DESTROYED);
    return ok;
}

static bool run_test_basic_send_recv(void)
{
    ipc_handle_t handle = IPC_HANDLE_INVALID;
    if (m_ipc_channel_create(2, IPC_CHANNEL_MAX_MESSAGE_SIZE, &handle) != IPC_OK) {
        return false;
    }

    const char payload[] = "ping";
    bool ok = (m_ipc_channel_send(handle, payload, sizeof(payload) - 1) == IPC_OK);
    char buffer[IPC_CHANNEL_MAX_MESSAGE_SIZE] = {0};
    size_t received = 0;
    ok &= (m_ipc_channel_recv(handle, buffer, sizeof(buffer), &received) == IPC_OK);
    ok &= (received == sizeof(payload) - 1);
    ok &= (memcmp(buffer, payload, received) == 0);
    ok &= (m_ipc_channel_destroy(handle) == IPC_OK);
    return ok;
}

static bool run_test_non_blocking_behavior(void)
{
    ipc_handle_t handle = IPC_HANDLE_INVALID;
    if (m_ipc_channel_create(1, 8, &handle) != IPC_OK) {
        return false;
    }

    bool ok = true;
    ok &= (m_ipc_channel_try_send(handle, "A", 1) == IPC_OK);
    ok &= (m_ipc_channel_try_send(handle, "B", 1) == IPC_ERR_NO_SPACE);

    char buffer[IPC_CHANNEL_MAX_MESSAGE_SIZE] = {0};
    size_t received = 0;
    ok &= (m_ipc_channel_try_recv(handle, buffer, sizeof(buffer), &received) == IPC_OK);
    ok &= (received == 1 && buffer[0] == 'A');
    ok &= (m_ipc_channel_try_recv(handle, buffer, sizeof(buffer), &received)
           == IPC_ERR_NOT_READY);
    ok &= (m_ipc_channel_destroy(handle) == IPC_OK);
    return ok;
}

static bool run_test_blocking_behavior(void)
{
    bool ok = true;

    ipc_handle_t send_handle = IPC_HANDLE_INVALID;
    if (m_ipc_channel_create(1, 8, &send_handle) != IPC_OK) {
        ESP_LOGE(TAG, "send create failed");
        return false;
    }

    ok &= (m_ipc_channel_send(send_handle, "A", 1) == IPC_OK);

    StaticSemaphore_t send_storage;
    SemaphoreHandle_t send_done = xSemaphoreCreateBinaryStatic(&send_storage);
    if (send_done == NULL) {
        ESP_LOGE(TAG, "failed to create send semaphore");
        m_ipc_channel_destroy(send_handle);
        return false;
    }

    ipc_channel_send_worker_ctx_t send_ctx = {
        .handle = send_handle,
        .done = send_done,
        .result = IPC_ERR_SHUTDOWN,
        .length = 1,
    };
    send_ctx.message[0] = 'B';

    m_sched_task_id_t send_task = M_SCHED_TASK_ID_INVALID;
    m_sched_task_options_t send_opts = {
        .name = "ipc_chan_send",
        .entry = ipc_channel_send_worker,
        .argument = &send_ctx,
        .stack_depth = configMINIMAL_STACK_SIZE,
        .priority = (tskIDLE_PRIORITY + 2),
        .cpu_affinity = M_SCHED_CPU_AFFINITY_ANY,
    };

    if (m_sched_task_create(&send_opts, &send_task) != M_SCHED_OK) {
        ESP_LOGE(TAG, "failed to create send worker");
        m_ipc_channel_destroy(send_handle);
        return false;
    }
    (void)send_task;
    (void)send_task;

    m_sched_sleep_ms(5);

    char buffer[IPC_CHANNEL_MAX_MESSAGE_SIZE] = {0};
    size_t received = 0;
    ok &= (m_ipc_channel_recv(send_handle, buffer, sizeof(buffer), &received) == IPC_OK);
    ok &= (xSemaphoreTake(send_done, pdMS_TO_TICKS(500)) == pdTRUE);
    ok &= (send_ctx.result == IPC_OK);
    ok &= (m_ipc_channel_recv(send_handle, buffer, sizeof(buffer), &received) == IPC_OK);
    ok &= (received == 1 && buffer[0] == 'B');
    ok &= (m_ipc_channel_destroy(send_handle) == IPC_OK);

    ipc_handle_t recv_handle = IPC_HANDLE_INVALID;
    if (m_ipc_channel_create(1, 8, &recv_handle) != IPC_OK) {
        ESP_LOGE(TAG, "recv create failed");
        return false;
    }

    StaticSemaphore_t recv_storage;
    SemaphoreHandle_t recv_done = xSemaphoreCreateBinaryStatic(&recv_storage);
    if (recv_done == NULL) {
        ESP_LOGE(TAG, "failed to create recv semaphore");
        m_ipc_channel_destroy(recv_handle);
        return false;
    }

    ipc_channel_recv_worker_ctx_t recv_ctx = {
        .handle = recv_handle,
        .done = recv_done,
        .result = IPC_ERR_SHUTDOWN,
    };

    m_sched_task_id_t recv_task = M_SCHED_TASK_ID_INVALID;
    m_sched_task_options_t recv_opts = {
        .name = "ipc_chan_recv",
        .entry = ipc_channel_recv_worker,
        .argument = &recv_ctx,
        .stack_depth = configMINIMAL_STACK_SIZE,
        .priority = (tskIDLE_PRIORITY + 2),
        .cpu_affinity = M_SCHED_CPU_AFFINITY_ANY,
    };

    if (m_sched_task_create(&recv_opts, &recv_task) != M_SCHED_OK) {
        ESP_LOGE(TAG, "failed to create recv worker");
        m_ipc_channel_destroy(recv_handle);
        return false;
    }
    (void)recv_task;
    (void)recv_task;

    m_sched_sleep_ms(5);

    const char payload[] = "Z";
    ok &= (m_ipc_channel_send(recv_handle, payload, sizeof(payload) - 1) == IPC_OK);
    ok &= (xSemaphoreTake(recv_done, pdMS_TO_TICKS(500)) == pdTRUE);
    ok &= (recv_ctx.result == IPC_OK);
    ok &= (recv_ctx.received_length == sizeof(payload) - 1);
    ok &= (memcmp(recv_ctx.buffer, payload, recv_ctx.received_length) == 0);
    ok &= (m_ipc_channel_destroy(recv_handle) == IPC_OK);

    return ok;
}

static bool run_test_timed_waits(void)
{
    ipc_handle_t handle = IPC_HANDLE_INVALID;
    if (m_ipc_channel_create(1, 8, &handle) != IPC_OK) {
        return false;
    }

    bool ok = true;
    ok &= (m_ipc_channel_send(handle, "A", 1) == IPC_OK);
    ok &= (m_ipc_channel_timed_send(handle, "B", 1, 1000) == IPC_ERR_TIMEOUT);

    char buffer[IPC_CHANNEL_MAX_MESSAGE_SIZE] = {0};
    size_t received = 0;
    ok &= (m_ipc_channel_recv(handle, buffer, sizeof(buffer), &received) == IPC_OK);
    ok &= (m_ipc_channel_timed_recv(handle, buffer, sizeof(buffer), &received, 1000)
           == IPC_ERR_TIMEOUT);
    ok &= (m_ipc_channel_destroy(handle) == IPC_OK);
    return ok;
}

static bool run_test_fifo_ordering(void)
{
    ipc_handle_t handle = IPC_HANDLE_INVALID;
    if (m_ipc_channel_create(4, IPC_CHANNEL_MAX_MESSAGE_SIZE, &handle) != IPC_OK) {
        return false;
    }

    const char *messages[] = {"one", "two", "three"};
    size_t count = sizeof(messages) / sizeof(messages[0]);
    bool ok = true;

    for (size_t i = 0; i < count; i++) {
        ok &= (m_ipc_channel_send(handle, messages[i], strlen(messages[i])) == IPC_OK);
    }

    char buffer[IPC_CHANNEL_MAX_MESSAGE_SIZE] = {0};
    size_t received = 0;
    for (size_t i = 0; i < count; i++) {
        ok &= (m_ipc_channel_recv(handle, buffer, sizeof(buffer), &received) == IPC_OK);
        ok &= (received == strlen(messages[i]));
        ok &= (memcmp(buffer, messages[i], received) == 0);
    }

    ok &= (m_ipc_channel_destroy(handle) == IPC_OK);
    return ok;
}

static bool run_test_destroy_wakes_waiters(void)
{
    bool ok = true;

    ipc_handle_t send_handle = IPC_HANDLE_INVALID;
    if (m_ipc_channel_create(1, 8, &send_handle) != IPC_OK) {
        return false;
    }

    ok &= (m_ipc_channel_send(send_handle, "X", 1) == IPC_OK);

    StaticSemaphore_t send_storage;
    SemaphoreHandle_t send_done = xSemaphoreCreateBinaryStatic(&send_storage);
    if (send_done == NULL) {
        m_ipc_channel_destroy(send_handle);
        return false;
    }

    ipc_channel_send_worker_ctx_t send_ctx = {
        .handle = send_handle,
        .done = send_done,
        .result = IPC_ERR_SHUTDOWN,
        .length = 1,
    };
    send_ctx.message[0] = 'Y';

    m_sched_task_id_t send_task = M_SCHED_TASK_ID_INVALID;
    m_sched_task_options_t send_opts = {
        .name = "cd_send",
        .entry = ipc_channel_send_worker,
        .argument = &send_ctx,
        .stack_depth = configMINIMAL_STACK_SIZE,
        .priority = (tskIDLE_PRIORITY + 2),
        .cpu_affinity = M_SCHED_CPU_AFFINITY_ANY,
    };

    if (m_sched_task_create(&send_opts, &send_task) != M_SCHED_OK) {
        m_ipc_channel_destroy(send_handle);
        return false;
    }

    m_sched_sleep_ms(5);
    m_ipc_channel_destroy(send_handle);

    BaseType_t send_taken = xSemaphoreTake(send_done, pdMS_TO_TICKS(500));
    ok &= (send_taken == pdTRUE);
    ok &= (send_ctx.result == IPC_ERR_OBJECT_DESTROYED);

    ipc_handle_t recv_handle = IPC_HANDLE_INVALID;
    if (m_ipc_channel_create(1, 8, &recv_handle) != IPC_OK) {
        return false;
    }

    StaticSemaphore_t recv_storage;
    SemaphoreHandle_t recv_done = xSemaphoreCreateBinaryStatic(&recv_storage);
    if (recv_done == NULL) {
        m_ipc_channel_destroy(recv_handle);
        return false;
    }

    ipc_channel_recv_worker_ctx_t recv_ctx = {
        .handle = recv_handle,
        .done = recv_done,
        .result = IPC_ERR_SHUTDOWN,
    };

    m_sched_task_id_t recv_task = M_SCHED_TASK_ID_INVALID;
    m_sched_task_options_t recv_opts = {
        .name = "cd_recv",
        .entry = ipc_channel_recv_worker,
        .argument = &recv_ctx,
        .stack_depth = configMINIMAL_STACK_SIZE,
        .priority = (tskIDLE_PRIORITY + 2),
        .cpu_affinity = M_SCHED_CPU_AFFINITY_ANY,
    };

    if (m_sched_task_create(&recv_opts, &recv_task) != M_SCHED_OK) {
        m_ipc_channel_destroy(recv_handle);
        return false;
    }

    m_sched_sleep_ms(5);
    m_ipc_channel_destroy(recv_handle);

    BaseType_t recv_taken = xSemaphoreTake(recv_done, pdMS_TO_TICKS(500));
    ok &= (recv_taken == pdTRUE);
    ok &= (recv_ctx.result == IPC_ERR_OBJECT_DESTROYED);

    return ok;
}

static bool run_test_invalid_handle(void)
{
    bool ok = true;
    ok &= (m_ipc_channel_destroy(IPC_HANDLE_INVALID) == IPC_ERR_INVALID_HANDLE);
    ok &= (m_ipc_channel_send(IPC_HANDLE_INVALID, "x", 1) == IPC_ERR_INVALID_HANDLE);
    ok &= (m_ipc_channel_try_send(IPC_HANDLE_INVALID, "x", 1)
           == IPC_ERR_INVALID_HANDLE);
    ok &= (m_ipc_channel_timed_send(IPC_HANDLE_INVALID, "x", 1, 1000)
           == IPC_ERR_INVALID_HANDLE);

    char buffer[IPC_CHANNEL_MAX_MESSAGE_SIZE] = {0};
    size_t received = 0;
    ok &= (m_ipc_channel_recv(IPC_HANDLE_INVALID, buffer, sizeof(buffer), &received)
           == IPC_ERR_INVALID_HANDLE);
    ok &= (m_ipc_channel_try_recv(IPC_HANDLE_INVALID, buffer, sizeof(buffer), &received)
           == IPC_ERR_INVALID_HANDLE);
    ok &= (m_ipc_channel_timed_recv(IPC_HANDLE_INVALID, buffer, sizeof(buffer),
                                 &received, 1000)
           == IPC_ERR_INVALID_HANDLE);
    return ok;
}

static bool run_test_memory_exhaustion(void)
{
    ipc_handle_t handles[IPC_MAX_CHANNELS];
    for (size_t i = 0; i < IPC_MAX_CHANNELS; i++) {
        handles[i] = IPC_HANDLE_INVALID;
    }

    bool ok = true;
    for (size_t i = 0; i < IPC_MAX_CHANNELS; i++) {
        ipc_error_t result = m_ipc_channel_create(1, 8, &handles[i]);
        ok &= (result == IPC_OK);
    }

    ipc_handle_t extra = IPC_HANDLE_INVALID;
    ok &= (m_ipc_channel_create(1, 8, &extra) == IPC_ERR_NO_SPACE);

    if (extra != IPC_HANDLE_INVALID) {
        m_ipc_channel_destroy(extra);
    }

    for (size_t i = 0; i < IPC_MAX_CHANNELS; i++) {
        if (handles[i] != IPC_HANDLE_INVALID) {
            ok &= (m_ipc_channel_destroy(handles[i]) == IPC_OK);
        }
    }

    return ok;
}

static bool run_test_diag_info(void)
{
    ipc_handle_t handle = IPC_HANDLE_INVALID;
    if (m_ipc_channel_create(2, IPC_CHANNEL_MAX_MESSAGE_SIZE, &handle) != IPC_OK) {
        return false;
    }

    const char payload[] = "diag";
    bool ok = (m_ipc_channel_send(handle, payload, sizeof(payload) - 1) == IPC_OK);

    ipc_channel_info_t info = {0};
    ok &= (ipc_diag_channel_info(handle, &info) == IPC_OK);
    ok &= (info.capacity == 2);
    ok &= (info.depth == 1);
    ok &= (info.waiting_senders == 0);
    ok &= (info.waiting_receivers == 0);
    ok &= (info.ready == true);
    ok &= (info.destroyed == false);

    char buffer[IPC_CHANNEL_MAX_MESSAGE_SIZE] = {0};
    size_t received = 0;
    ok &= (m_ipc_channel_recv(handle, buffer, sizeof(buffer), &received) == IPC_OK);
    ok &= (m_ipc_channel_destroy(handle) == IPC_OK);
    return ok;
}

bool ipc_channel_tests_run(void)
{
    bool overall = true;
    overall &= test_report("channel create/destroy", run_test_create_destroy());
    overall &= test_report("channel send/recv", run_test_basic_send_recv());
    overall &= test_report("channel non-blocking", run_test_non_blocking_behavior());
    overall &= test_report("channel blocking", run_test_blocking_behavior());
    overall &= test_report("channel timed", run_test_timed_waits());
    overall &= test_report("channel FIFO", run_test_fifo_ordering());
    overall &= test_report("channel destroy wakes", run_test_destroy_wakes_waiters());
    overall &= test_report("channel invalid handle", run_test_invalid_handle());
    overall &= test_report("channel memory exhaustion", run_test_memory_exhaustion());
    overall &= test_report("channel diagnostics", run_test_diag_info());

    ESP_LOGI(TAG, "IPC channel self-tests %s",
             overall ? "PASSED" : "FAILED");
    return overall;
}

#else

#include "kernel/core/ipc/tests/ipc_channel_tests.h"

bool ipc_channel_tests_run(void)
{
    return true;
}

#endif /* CONFIG_MAGNOLIA_IPC_ENABLED && CONFIG_MAGNOLIA_IPC_SELFTESTS */
