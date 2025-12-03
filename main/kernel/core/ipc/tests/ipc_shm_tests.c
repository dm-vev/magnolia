#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"

#ifdef CONFIG_MAGNOLIA_IPC_SELFTESTS

#include <string.h>

#include "esp_log.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "kernel/core/ipc/ipc_diag.h"
#include "kernel/core/ipc/ipc_shm.h"
#include "kernel/core/ipc/tests/ipc_shm_tests.h"
#include "kernel/core/sched/m_sched.h"
#include "kernel/core/timer/m_timer.h"

static const char *TAG = "ipc_shm_tests";

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
    ipc_shm_attachment_t attachment;
    SemaphoreHandle_t done;
    volatile ipc_error_t result;
    uint8_t buffer[16];
} ipc_shm_reader_ctx_t;

typedef struct {
    ipc_shm_attachment_t attachment;
    SemaphoreHandle_t done;
    volatile ipc_error_t result;
    const uint8_t *payload;
    size_t length;
} ipc_shm_writer_ctx_t;

static void ipc_shm_reader_worker(void *arg)
{
    ipc_shm_reader_ctx_t *ctx = arg;
    if (ctx == NULL || ctx->done == NULL) {
        return;
    }

    ctx->result = ipc_shm_read(&ctx->attachment,
                               ctx->buffer,
                               sizeof(ctx->buffer),
                               NULL);
    xSemaphoreGive(ctx->done);
}

static void ipc_shm_writer_worker(void *arg)
{
    ipc_shm_writer_ctx_t *ctx = arg;
    if (ctx == NULL || ctx->done == NULL || ctx->payload == NULL
        || ctx->length == 0) {
        return;
    }

    ctx->result = ipc_shm_write(&ctx->attachment, ctx->payload, ctx->length);
    xSemaphoreGive(ctx->done);
}

static bool run_test_create_destroy(void)
{
    ipc_handle_t handle = IPC_HANDLE_INVALID;
    if (ipc_shm_create(32,
                       IPC_SHM_MODE_RING_BUFFER,
                       NULL,
                       &handle) != IPC_OK) {
        return false;
    }

    ipc_shm_attachment_t reader = {0};
    ipc_shm_attachment_t writer = {0};
    if (ipc_shm_attach(handle,
                       IPC_SHM_ACCESS_READ_ONLY,
                       NULL,
                       &reader) != IPC_OK
        || ipc_shm_attach(handle,
                          IPC_SHM_ACCESS_WRITE_ONLY,
                          NULL,
                          &writer) != IPC_OK) {
        ipc_shm_destroy(handle);
        return false;
    }

    const uint8_t sample[3] = {1, 2, 3};
    bool ok = (ipc_shm_write(&writer, sample, sizeof(sample)) == IPC_OK);

    uint8_t scratch[3] = {0};
    ok &= (ipc_shm_read(&reader, scratch, sizeof(scratch), NULL) == IPC_OK);
    ok &= (memcmp(sample, scratch, sizeof(sample)) == 0);

    ipc_shm_destroy(handle);
    ok &= (ipc_shm_read(&reader, scratch, sizeof(scratch), NULL)
           == IPC_ERR_OBJECT_DESTROYED);

    ok &= (ipc_shm_detach(&reader) == IPC_OK);
    ok &= (ipc_shm_detach(&writer) == IPC_OK);
    return ok;
}

static bool run_test_attach_permissions(void)
{
    ipc_handle_t handle = IPC_HANDLE_INVALID;
    if (ipc_shm_create(16,
                       IPC_SHM_MODE_RING_BUFFER,
                       NULL,
                       &handle) != IPC_OK) {
        return false;
    }

    ipc_shm_attachment_t read_only = {0};
    ipc_shm_attachment_t write_only = {0};
    bool ok = (ipc_shm_attach(handle,
                              IPC_SHM_ACCESS_READ_ONLY,
                              NULL,
                              &read_only) == IPC_OK);
    ok &= (ipc_shm_attach(handle,
                          IPC_SHM_ACCESS_WRITE_ONLY,
                          NULL,
                          &write_only) == IPC_OK);

    ok &= (ipc_shm_write(&read_only, "X", 1) == IPC_ERR_NO_PERMISSION);
    uint8_t peek[1];
    ok &= (ipc_shm_read(&write_only, peek, sizeof(peek), NULL)
           == IPC_ERR_NO_PERMISSION);

    ok &= (ipc_shm_detach(&read_only) == IPC_OK);
    ok &= (ipc_shm_detach(&write_only) == IPC_OK);
    ipc_shm_destroy(handle);
    return ok;
}

static bool run_test_ring_basic(void)
{
    ipc_handle_t handle = IPC_HANDLE_INVALID;
    if (ipc_shm_create(32,
                       IPC_SHM_MODE_RING_BUFFER,
                       NULL,
                       &handle) != IPC_OK) {
        return false;
    }

    ipc_shm_attachment_t reader = {0};
    ipc_shm_attachment_t writer = {0};
    if (ipc_shm_attach(handle,
                       IPC_SHM_ACCESS_READ_WRITE,
                       NULL,
                       &reader) != IPC_OK
        || ipc_shm_attach(handle,
                          IPC_SHM_ACCESS_READ_WRITE,
                          NULL,
                          &writer) != IPC_OK) {
        ipc_shm_destroy(handle);
        return false;
    }

    const uint8_t sequence[] = {10, 11, 12, 13};
    bool ok = (ipc_shm_write(&writer, sequence, sizeof(sequence)) == IPC_OK);

    uint8_t buffer[sizeof(sequence)];
    ok &= (ipc_shm_read(&reader, buffer, sizeof(buffer), NULL) == IPC_OK);
    ok &= (memcmp(sequence, buffer, sizeof(sequence)) == 0);

    ok &= (ipc_shm_detach(&reader) == IPC_OK);
    ok &= (ipc_shm_detach(&writer) == IPC_OK);
    ipc_shm_destroy(handle);
    return ok;
}

static bool run_test_packet_mode(void)
{
    ipc_handle_t handle = IPC_HANDLE_INVALID;
    ipc_shm_region_options_t opts = {.packet_max_payload = 32};
    if (ipc_shm_create(64,
                       IPC_SHM_MODE_PACKET_BUFFER,
                       &opts,
                       &handle) != IPC_OK) {
        return false;
    }

    ipc_shm_attachment_t reader = {0};
    ipc_shm_attachment_t writer = {0};
    if (ipc_shm_attach(handle,
                       IPC_SHM_ACCESS_READ_ONLY,
                       NULL,
                       &reader) != IPC_OK
        || ipc_shm_attach(handle,
                          IPC_SHM_ACCESS_WRITE_ONLY,
                          NULL,
                          &writer) != IPC_OK) {
        ipc_shm_destroy(handle);
        return false;
    }

    const uint8_t first[] = "hello";
    const uint8_t second[] = "packet";
    bool ok = (ipc_shm_write(&writer, first, sizeof(first)) == IPC_OK);
    ok &= (ipc_shm_write(&writer, second, sizeof(second)) == IPC_OK);

    uint8_t buf[16] = {0};
    size_t transferred = 0;
    ok &= (ipc_shm_read(&reader, buf, sizeof(buf), &transferred) == IPC_OK);
    ok &= ((transferred == sizeof(first)) && (memcmp(buf, first, transferred) == 0));

    memset(buf, 0, sizeof(buf));
    ok &= (ipc_shm_read(&reader, buf, sizeof(buf), &transferred) == IPC_OK);
    ok &= ((transferred == sizeof(second)) && (memcmp(buf, second, transferred) == 0));

    ok &= (ipc_shm_try_read(&reader, buf, sizeof(buf), &transferred) == IPC_ERR_EMPTY);
    ok &= (ipc_shm_detach(&reader) == IPC_OK);
    ok &= (ipc_shm_detach(&writer) == IPC_OK);
    ipc_shm_destroy(handle);
    return ok;
}

static bool run_test_blocking_read(void)
{
    ipc_handle_t handle = IPC_HANDLE_INVALID;
    if (ipc_shm_create(32,
                       IPC_SHM_MODE_RING_BUFFER,
                       NULL,
                       &handle) != IPC_OK) {
        return false;
    }

    ipc_shm_attachment_t writer = {0};
    if (ipc_shm_attach(handle,
                       IPC_SHM_ACCESS_WRITE_ONLY,
                       NULL,
                       &writer) != IPC_OK) {
        ipc_shm_destroy(handle);
        return false;
    }

    static StaticSemaphore_t storage;
    SemaphoreHandle_t done = xSemaphoreCreateBinaryStatic(&storage);
    if (done == NULL) {
        ipc_shm_detach(&writer);
        ipc_shm_destroy(handle);
        return false;
    }

    ipc_shm_reader_ctx_t ctx = {.done = done, .result = IPC_ERR_SHUTDOWN};
    if (ipc_shm_attach(handle,
                       IPC_SHM_ACCESS_READ_ONLY,
                       NULL,
                       &ctx.attachment) != IPC_OK) {
        ipc_shm_detach(&writer);
        ipc_shm_destroy(handle);
        return false;
    }

    m_sched_task_options_t opts = {
        .name = "ipc_shm_reader",
        .entry = ipc_shm_reader_worker,
        .argument = &ctx,
        .stack_depth = configMINIMAL_STACK_SIZE,
        .priority = (tskIDLE_PRIORITY + 1),
        .cpu_affinity = M_SCHED_CPU_AFFINITY_ANY,
    };

    m_sched_task_id_t task_id = M_SCHED_TASK_ID_INVALID;
    if (m_sched_task_create(&opts, &task_id) != M_SCHED_OK) {
        ipc_shm_detach(&ctx.attachment);
        ipc_shm_detach(&writer);
        ipc_shm_destroy(handle);
        return false;
    }

    m_sched_sleep_ms(5);
    const uint8_t payload[] = "ok";
    ipc_shm_write(&writer, payload, sizeof(payload));

    bool ok = (xSemaphoreTake(done, pdMS_TO_TICKS(500)) == pdTRUE);
    ok &= (ctx.result == IPC_OK);
    ok &= (memcmp(payload, ctx.buffer, sizeof(payload)) == 0);

    ipc_shm_detach(&ctx.attachment);
    ipc_shm_detach(&writer);
    ipc_shm_destroy(handle);
    return ok;
}

static bool run_test_blocking_write(void)
{
    ipc_handle_t handle = IPC_HANDLE_INVALID;
    if (ipc_shm_create(32,
                       IPC_SHM_MODE_RING_BUFFER,
                       NULL,
                       &handle) != IPC_OK) {
        return false;
    }

    ipc_shm_attachment_t filler = {0};
    if (ipc_shm_attach(handle,
                       IPC_SHM_ACCESS_WRITE_ONLY,
                       NULL,
                       &filler) != IPC_OK) {
        ipc_shm_destroy(handle);
        return false;
    }

    ipc_shm_attachment_t reader = {0};
    if (ipc_shm_attach(handle,
                       IPC_SHM_ACCESS_READ_ONLY,
                       NULL,
                       &reader) != IPC_OK) {
        ipc_shm_detach(&filler);
        ipc_shm_destroy(handle);
        return false;
    }

    const uint8_t chunk[16] = {0};
    ipc_shm_write(&filler, chunk, sizeof(chunk));

    static StaticSemaphore_t storage;
    SemaphoreHandle_t done = xSemaphoreCreateBinaryStatic(&storage);
    if (done == NULL) {
        ipc_shm_detach(&reader);
        ipc_shm_detach(&filler);
        ipc_shm_destroy(handle);
        return false;
    }

    ipc_shm_writer_ctx_t ctx = {
        .done = done,
        .payload = (const uint8_t *)"W",
        .length = 1,
    };

    if (ipc_shm_attach(handle,
                       IPC_SHM_ACCESS_WRITE_ONLY,
                       NULL,
                       &ctx.attachment) != IPC_OK) {
        ipc_shm_detach(&reader);
        ipc_shm_detach(&filler);
        ipc_shm_destroy(handle);
        return false;
    }

    m_sched_task_options_t opts = {
        .name = "ipc_shm_writer",
        .entry = ipc_shm_writer_worker,
        .argument = &ctx,
        .stack_depth = configMINIMAL_STACK_SIZE,
        .priority = (tskIDLE_PRIORITY + 1),
        .cpu_affinity = M_SCHED_CPU_AFFINITY_ANY,
    };

    m_sched_task_id_t task_id = M_SCHED_TASK_ID_INVALID;
    if (m_sched_task_create(&opts, &task_id) != M_SCHED_OK) {
        ipc_shm_detach(&ctx.attachment);
        ipc_shm_detach(&reader);
        ipc_shm_detach(&filler);
        ipc_shm_destroy(handle);
        return false;
    }

    m_sched_sleep_ms(5);
    uint8_t sink[4];
    ipc_shm_read(&reader, sink, sizeof(sink), NULL);

    bool ok = (xSemaphoreTake(done, pdMS_TO_TICKS(500)) == pdTRUE);
    ok &= (ctx.result == IPC_OK);

    ipc_shm_detach(&ctx.attachment);
    ipc_shm_detach(&reader);
    ipc_shm_detach(&filler);
    ipc_shm_destroy(handle);
    return ok;
}

static bool run_test_timed_wait(void)
{
    ipc_handle_t handle = IPC_HANDLE_INVALID;
    if (ipc_shm_create(32,
                       IPC_SHM_MODE_RING_BUFFER,
                       NULL,
                       &handle) != IPC_OK) {
        return false;
    }

    ipc_shm_attachment_t reader = {0};
    ipc_shm_attachment_t writer = {0};
    if (ipc_shm_attach(handle,
                       IPC_SHM_ACCESS_READ_ONLY,
                       NULL,
                       &reader) != IPC_OK
        || ipc_shm_attach(handle,
                          IPC_SHM_ACCESS_WRITE_ONLY,
                          NULL,
                          &writer) != IPC_OK) {
        ipc_shm_destroy(handle);
        return false;
    }

    uint8_t buffer[4];
    bool ok = (ipc_shm_read_timed(&reader,
                                  buffer,
                                  sizeof(buffer),
                                  NULL,
                                  1000) == IPC_ERR_TIMEOUT);

    uint8_t chunk[16] = {0};
    ipc_shm_write(&writer, chunk, sizeof(chunk));

    ok &= (ipc_shm_write_timed(&writer, chunk, sizeof(chunk), 1000)
           == IPC_ERR_TIMEOUT);

    ok &= (ipc_shm_detach(&reader) == IPC_OK);
    ok &= (ipc_shm_detach(&writer) == IPC_OK);
    ipc_shm_destroy(handle);
    return ok;
}

static bool run_test_destroy_wakes_waiters(void)
{
    ipc_handle_t handle = IPC_HANDLE_INVALID;
    if (ipc_shm_create(32,
                       IPC_SHM_MODE_RING_BUFFER,
                       NULL,
                       &handle) != IPC_OK) {
        return false;
    }

    static StaticSemaphore_t storage;
    SemaphoreHandle_t done = xSemaphoreCreateBinaryStatic(&storage);
    if (done == NULL) {
        ipc_shm_destroy(handle);
        return false;
    }

    ipc_shm_reader_ctx_t ctx = {.done = done, .result = IPC_ERR_SHUTDOWN};
    if (ipc_shm_attach(handle,
                       IPC_SHM_ACCESS_READ_ONLY,
                       NULL,
                       &ctx.attachment) != IPC_OK) {
        ipc_shm_destroy(handle);
        return false;
    }

    m_sched_task_options_t opts = {
        .name = "ipc_shm_destroy",
        .entry = ipc_shm_reader_worker,
        .argument = &ctx,
        .stack_depth = configMINIMAL_STACK_SIZE,
        .priority = (tskIDLE_PRIORITY + 1),
        .cpu_affinity = M_SCHED_CPU_AFFINITY_ANY,
    };

    m_sched_task_id_t task_id = M_SCHED_TASK_ID_INVALID;
    if (m_sched_task_create(&opts, &task_id) != M_SCHED_OK) {
        ipc_shm_detach(&ctx.attachment);
        ipc_shm_destroy(handle);
        return false;
    }

    m_sched_sleep_ms(5);
    ipc_shm_destroy(handle);

    bool ok = (xSemaphoreTake(done, pdMS_TO_TICKS(500)) == pdTRUE);
    ok &= (ctx.result == IPC_ERR_OBJECT_DESTROYED);

    ipc_shm_detach(&ctx.attachment);
    return ok;
}

static bool run_test_control_flush(void)
{
    ipc_handle_t handle = IPC_HANDLE_INVALID;
    if (ipc_shm_create(32,
                       IPC_SHM_MODE_RING_BUFFER,
                       NULL,
                       &handle) != IPC_OK) {
        return false;
    }

    ipc_shm_attachment_t reader = {0};
    ipc_shm_attachment_t writer = {0};
    if (ipc_shm_attach(handle,
                       IPC_SHM_ACCESS_READ_WRITE,
                       NULL,
                       &reader) != IPC_OK
        || ipc_shm_attach(handle,
                          IPC_SHM_ACCESS_READ_WRITE,
                          NULL,
                          &writer) != IPC_OK) {
        ipc_shm_destroy(handle);
        return false;
    }

    const uint8_t sample[] = "data";
    ipc_shm_write(&writer, sample, sizeof(sample));
    ipc_shm_control(handle, IPC_SHM_CONTROL_FLUSH, NULL);

    uint8_t buffer[8];
    size_t transferred = 0;
    bool ok = (ipc_shm_try_read(&reader, buffer, sizeof(buffer), &transferred)
               == IPC_ERR_EMPTY);
    ok &= (transferred == 0);

    ipc_shm_detach(&reader);
    ipc_shm_detach(&writer);
    ipc_shm_destroy(handle);
    return ok;
}

static bool run_test_query_info(void)
{
    ipc_handle_t handle = IPC_HANDLE_INVALID;
    if (ipc_shm_create(64,
                       IPC_SHM_MODE_RING_BUFFER,
                       NULL,
                       &handle) != IPC_OK) {
        return false;
    }

    ipc_shm_info_t info = {0};
    bool ok = (ipc_shm_query(handle, &info) == IPC_OK);
    ok &= (info.region_size == 64);
    ok &= (info.mode == IPC_SHM_MODE_RING_BUFFER);
    ok &= (info.destroyed == false);

    ipc_shm_destroy(handle);
    return ok;
}

bool ipc_shm_tests_run(void)
{
    bool overall = true;
    overall &= test_report("shm create/destroy", run_test_create_destroy());
    overall &= test_report("shm permissions", run_test_attach_permissions());
    overall &= test_report("ring basic read/write", run_test_ring_basic());
    overall &= test_report("packet mode", run_test_packet_mode());
    overall &= test_report("blocking read", run_test_blocking_read());
    overall &= test_report("blocking write", run_test_blocking_write());
    overall &= test_report("timed waits", run_test_timed_wait());
    overall &= test_report("destroy wakes waiters",
                           run_test_destroy_wakes_waiters());
    overall &= test_report("control flush", run_test_control_flush());
    overall &= test_report("query info", run_test_query_info());

    ESP_LOGI(TAG, "SHM self-tests %s", overall ? "PASSED" : "FAILED");
    return overall;
}

#else

#include "kernel/core/ipc/tests/ipc_shm_tests.h"

bool ipc_shm_tests_run(void)
{
    return true;
}

#endif /* CONFIG_MAGNOLIA_IPC_SELFTESTS */
