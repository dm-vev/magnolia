#include "sdkconfig.h"

#ifdef CONFIG_MAGNOLIA_JOB_SELFTESTS

#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "kernel/core/job/m_job.h"
#include "kernel/core/job/tests/m_job_tests.h"
#include "kernel/core/timer/m_timer.h"

static const char *TAG = "job_tests";

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
    SemaphoreHandle_t done;
    volatile int count;
} job_test_context_t;

typedef struct {
    const char *payload;
    size_t size;
} job_result_payload_ctx_t;

static const char job_success_payload[] = "job-success";
static const char job_error_payload[] = "job-error";

static m_job_result_descriptor_t job_increment(m_job_id_t job, void *arg)
{
    job_test_context_t *ctx = arg;
    ctx->count++;
    xSemaphoreGive(ctx->done);
    (void)job;
    return m_job_result_success(NULL, 0);
}

static m_job_result_descriptor_t job_noop(m_job_id_t job, void *arg)
{
    (void)job;
    (void)arg;
    return m_job_result_success(NULL, 0);
}

static m_job_result_descriptor_t job_sleepy(m_job_id_t job, void *arg)
{
    (void)job;
    (void)arg;
    m_sched_sleep_ms(50);
    return m_job_result_success(NULL, 0);
}

static m_job_result_descriptor_t job_result_payload(m_job_id_t job, void *arg)
{
    job_result_payload_ctx_t *ctx = arg;
    return m_job_result_success(ctx->payload, ctx->size);
}

static m_job_result_descriptor_t job_result_error(m_job_id_t job, void *arg)
{
    job_result_payload_ctx_t *ctx = arg;
    return m_job_result_error(ctx->payload, ctx->size);
}

static m_job_error_t g_block_submit_result = M_JOB_OK;
static SemaphoreHandle_t g_block_submit_done = NULL;

static void job_blocking_submitter(void *arg)
{
    m_job_queue_t *queue = arg;
    g_block_submit_result = m_job_queue_submit(queue, job_noop, NULL);
    xSemaphoreGive(g_block_submit_done);
    vTaskDelete(NULL);
}

static bool run_test_job_execution(void)
{
    m_job_queue_config_t config = M_JOB_QUEUE_CONFIG_DEFAULT;
    config.capacity = 4;
    config.worker_count = 2;

    m_job_queue_t *queue = m_job_queue_create(&config);
    if (queue == NULL) {
        return false;
    }

    static StaticSemaphore_t storage;
    SemaphoreHandle_t done = xSemaphoreCreateCountingStatic(4, 0, &storage);
    if (done == NULL) {
        m_job_queue_destroy(queue);
        return false;
    }

    job_test_context_t ctx = {.done = done, .count = 0};
    bool ok = true;
    for (int i = 0; i < 4; ++i) {
        if (m_job_queue_submit(queue, job_increment, &ctx) != M_JOB_OK) {
            ok = false;
            break;
        }
    }

    for (int i = 0; i < 4 && ok; ++i) {
        if (xSemaphoreTake(done, pdMS_TO_TICKS(1000)) != pdTRUE) {
            ok = false;
        }
    }

    m_job_stats_t stats = {0};
    m_job_queue_get_stats(queue, &stats);
    ok &= (ctx.count == 4);
    ok &= (stats.submitted == 4);
    ok &= (stats.executed == 4);
    ok &= (stats.failed == 0);
    m_job_queue_destroy(queue);
    return ok;
}

static bool run_test_queue_full(void)
{
    m_job_queue_config_t config = M_JOB_QUEUE_CONFIG_DEFAULT;
    config.capacity = 1;
    config.worker_count = 1;

    m_job_queue_t *queue = m_job_queue_create(&config);
    if (queue == NULL) {
        return false;
    }

    m_sched_task_id_t worker_id = m_job_queue_get_worker_task_id(queue, 0);
    bool suspended = false;
    if (worker_id != M_SCHED_TASK_ID_INVALID) {
        suspended = (m_sched_task_suspend(worker_id) == M_SCHED_OK);
    }
    bool ok = true;
    if (m_job_queue_submit(queue, job_noop, NULL) != M_JOB_OK) {
        ok = false;
    }
    if (ok) {
        ok = (m_job_queue_submit_nowait(queue, job_noop, NULL)
              == M_JOB_ERR_QUEUE_FULL);
    }
    if (suspended) {
        m_sched_task_resume(worker_id);
    }
    m_job_queue_destroy(queue);
    return ok;
}

static bool run_test_timeout_submission(void)
{
    m_job_queue_config_t config = M_JOB_QUEUE_CONFIG_DEFAULT;
    config.capacity = 1;
    config.worker_count = 1;

    m_job_queue_t *queue = m_job_queue_create(&config);
    if (queue == NULL) {
        return false;
    }

    m_sched_task_id_t worker_id = m_job_queue_get_worker_task_id(queue, 0);
    bool suspended = false;
    if (worker_id != M_SCHED_TASK_ID_INVALID) {
        suspended = (m_sched_task_suspend(worker_id) == M_SCHED_OK);
    }
    if (m_job_queue_submit(queue, job_noop, NULL) != M_JOB_OK) {
        if (suspended) {
            m_sched_task_resume(worker_id);
        }
        m_job_queue_destroy(queue);
        return false;
    }

    m_timer_deadline_t deadline = m_timer_deadline_from_relative(2000ULL);
    m_job_error_t err = m_job_queue_submit_until(queue,
                                                 job_noop,
                                                 NULL,
                                                 &deadline);
    if (suspended) {
        m_sched_task_resume(worker_id);
    }
    bool ok = (err == M_JOB_ERR_TIMEOUT);
    m_job_queue_destroy(queue);
    return ok;
}

static bool run_test_destroy_while_submitting(void)
{
    m_job_queue_config_t config = M_JOB_QUEUE_CONFIG_DEFAULT;
    config.capacity = 1;
    config.worker_count = 1;

    m_job_queue_t *queue = m_job_queue_create(&config);
    if (queue == NULL) {
        return false;
    }

    m_sched_task_id_t worker_id = m_job_queue_get_worker_task_id(queue, 0);
    bool suspended = false;
    if (worker_id != M_SCHED_TASK_ID_INVALID) {
        suspended = (m_sched_task_suspend(worker_id) == M_SCHED_OK);
    }

    if (m_job_queue_submit(queue, job_sleepy, NULL) != M_JOB_OK) {
        if (suspended) {
            m_sched_task_resume(worker_id);
        }
        m_job_queue_destroy(queue);
        return false;
    }

    static StaticSemaphore_t storage;
    g_block_submit_done = xSemaphoreCreateBinaryStatic(&storage);
    if (g_block_submit_done == NULL) {
        m_job_queue_destroy(queue);
        return false;
    }

    BaseType_t created = xTaskCreate(job_blocking_submitter,
                                     "job_blocker",
                                     configMINIMAL_STACK_SIZE,
                                     queue,
                                     tskIDLE_PRIORITY,
                                     NULL);
    if (created != pdPASS) {
        m_job_queue_destroy(queue);
        return false;
    }

    m_sched_sleep_ms(5);

    m_job_error_t destroy_res = m_job_queue_destroy(queue);
    bool ok = (destroy_res == M_JOB_OK);
    if (xSemaphoreTake(g_block_submit_done, pdMS_TO_TICKS(1000)) == pdTRUE) {
        ok &= (g_block_submit_result == M_JOB_ERR_DESTROYED);
    } else {
        ok = false;
    }

    return ok;
}

static bool run_test_job_result_success(void)
{
    m_job_queue_config_t config = M_JOB_QUEUE_CONFIG_DEFAULT;
    config.capacity = 2;
    config.worker_count = 1;

    job_result_payload_ctx_t ctx = {.payload = job_success_payload,
                                    .size = sizeof(job_success_payload) - 1};

    m_job_queue_t *queue = m_job_queue_create(&config);
    if (queue == NULL) {
        return false;
    }

    m_job_handle_t *job = NULL;
    if (m_job_queue_submit_with_handle(queue,
                                       job_result_payload,
                                       &ctx,
                                       &job)
        != M_JOB_OK) {
        m_job_queue_destroy(queue);
        return false;
    }

    m_job_result_descriptor_t result = {0};
    bool ok = (m_job_wait_for_job(job, &result) == M_JOB_FUTURE_WAIT_OK
               && result.status == M_JOB_RESULT_SUCCESS
               && result.payload == job_success_payload
               && result.payload_size == ctx.size);
    m_job_handle_destroy(job);
    m_job_queue_destroy(queue);
    return ok;
}

static bool run_test_job_result_error(void)
{
    m_job_queue_config_t config = M_JOB_QUEUE_CONFIG_DEFAULT;
    config.capacity = 2;
    config.worker_count = 1;

    job_result_payload_ctx_t ctx = {.payload = job_error_payload,
                                    .size = sizeof(job_error_payload) - 1};

    m_job_queue_t *queue = m_job_queue_create(&config);
    if (queue == NULL) {
        return false;
    }

    m_job_handle_t *job = NULL;
    if (m_job_queue_submit_with_handle(queue,
                                       job_result_error,
                                       &ctx,
                                       &job)
        != M_JOB_OK) {
        m_job_queue_destroy(queue);
        return false;
    }

    m_job_result_descriptor_t result = {0};
    bool ok = (m_job_wait_for_job(job, &result) == M_JOB_FUTURE_WAIT_OK
               && result.status == M_JOB_RESULT_ERROR
               && result.payload == job_error_payload
               && result.payload_size == ctx.size);
    m_job_handle_destroy(job);
    m_job_queue_destroy(queue);
    return ok;
}

static bool run_test_job_result_cancelled(void)
{
    m_job_queue_config_t config = M_JOB_QUEUE_CONFIG_DEFAULT;
    config.capacity = 2;
    config.worker_count = 1;

    m_job_queue_t *queue = m_job_queue_create(&config);
    if (queue == NULL) {
        return false;
    }

    m_sched_task_id_t worker_id = m_job_queue_get_worker_task_id(queue, 0);
    bool suspended = (worker_id != M_SCHED_TASK_ID_INVALID)
                     && (m_sched_task_suspend(worker_id) == M_SCHED_OK);

    m_job_handle_t *job = NULL;
    if (m_job_queue_submit_with_handle(queue,
                                       job_sleepy,
                                       NULL,
                                       &job)
        != M_JOB_OK) {
        if (suspended) {
            m_sched_task_resume(worker_id);
        }
        m_job_queue_destroy(queue);
        return false;
    }

    bool cancelled = (m_job_cancel(job) == M_JOB_OK);
    if (suspended) {
        m_sched_task_resume(worker_id);
    }

    m_job_result_descriptor_t result = {0};
    bool ok = cancelled
              && (m_job_wait_for_job(job, &result) == M_JOB_FUTURE_WAIT_OK)
              && (result.status == M_JOB_RESULT_CANCELLED);
    m_job_handle_destroy(job);
    m_job_queue_destroy(queue);
    return ok;
}

static bool run_test_job_result_not_ready(void)
{
    m_job_queue_config_t config = M_JOB_QUEUE_CONFIG_DEFAULT;
    config.capacity = 1;
    config.worker_count = 1;

    m_job_queue_t *queue = m_job_queue_create(&config);
    if (queue == NULL) {
        return false;
    }

    m_job_handle_t *job = NULL;
    if (m_job_queue_submit_with_handle(queue,
                                       job_sleepy,
                                       NULL,
                                       &job)
        != M_JOB_OK) {
        m_job_queue_destroy(queue);
        return false;
    }

    m_job_result_descriptor_t peek = {0};
    bool ok = (m_job_query_result(job, &peek) == M_JOB_ERR_NOT_READY);
    m_job_result_descriptor_t final_result = {0};
    ok &= (m_job_wait_for_job(job, &final_result) == M_JOB_FUTURE_WAIT_OK);
    ok &= (final_result.status == M_JOB_RESULT_SUCCESS);
    m_job_handle_destroy(job);
    m_job_queue_destroy(queue);
    return ok;
}

static bool run_test_future_wait_success(void)
{
    m_job_queue_config_t config = M_JOB_QUEUE_CONFIG_DEFAULT;
    config.capacity = 2;
    config.worker_count = 1;

    job_result_payload_ctx_t ctx = {.payload = job_success_payload,
                                    .size = sizeof(job_success_payload) - 1};

    m_job_queue_t *queue = m_job_queue_create(&config);
    if (queue == NULL) {
        return false;
    }

    m_job_handle_t *job = NULL;
    if (m_job_queue_submit_with_handle(queue,
                                       job_result_payload,
                                       &ctx,
                                       &job)
        != M_JOB_OK) {
        m_job_queue_destroy(queue);
        return false;
    }

    m_job_future_t future = {0};
    bool ok = (m_job_future_init(&future, job) == M_JOB_OK);
    m_job_result_descriptor_t result = {0};
    ok &= (m_job_future_wait(&future, NULL, &result)
           == M_JOB_FUTURE_WAIT_OK);
    ok &= (result.status == M_JOB_RESULT_SUCCESS);
    m_job_future_deinit(&future);
    m_job_handle_destroy(job);
    m_job_queue_destroy(queue);
    return ok;
}

static bool run_test_future_wait_error(void)
{
    m_job_queue_config_t config = M_JOB_QUEUE_CONFIG_DEFAULT;
    config.capacity = 2;
    config.worker_count = 1;

    job_result_payload_ctx_t ctx = {.payload = job_error_payload,
                                    .size = sizeof(job_error_payload) - 1};

    m_job_queue_t *queue = m_job_queue_create(&config);
    if (queue == NULL) {
        return false;
    }

    m_job_handle_t *job = NULL;
    if (m_job_queue_submit_with_handle(queue,
                                       job_result_error,
                                       &ctx,
                                       &job)
        != M_JOB_OK) {
        m_job_queue_destroy(queue);
        return false;
    }

    m_job_future_t future = {0};
    bool ok = (m_job_future_init(&future, job) == M_JOB_OK);
    m_job_result_descriptor_t result = {0};
    ok &= (m_job_future_wait(&future, NULL, &result)
           == M_JOB_FUTURE_WAIT_OK);
    ok &= (result.status == M_JOB_RESULT_ERROR);
    m_job_future_deinit(&future);
    m_job_handle_destroy(job);
    m_job_queue_destroy(queue);
    return ok;
}

static bool run_test_future_wait_cancelled(void)
{
    m_job_queue_config_t config = M_JOB_QUEUE_CONFIG_DEFAULT;
    config.capacity = 2;
    config.worker_count = 1;

    m_job_queue_t *queue = m_job_queue_create(&config);
    if (queue == NULL) {
        return false;
    }

    m_sched_task_id_t worker_id = m_job_queue_get_worker_task_id(queue, 0);
    bool suspended = (worker_id != M_SCHED_TASK_ID_INVALID)
                     && (m_sched_task_suspend(worker_id) == M_SCHED_OK);

    m_job_handle_t *job = NULL;
    if (m_job_queue_submit_with_handle(queue,
                                       job_sleepy,
                                       NULL,
                                       &job)
        != M_JOB_OK) {
        if (suspended) {
            m_sched_task_resume(worker_id);
        }
        m_job_queue_destroy(queue);
        return false;
    }

    m_job_future_t future = {0};
    bool ok = (m_job_future_init(&future, job) == M_JOB_OK);
    ok &= (m_job_cancel(job) == M_JOB_OK);
    if (suspended) {
        m_sched_task_resume(worker_id);
    }

    m_job_result_descriptor_t result = {0};
    ok &= (m_job_future_wait(&future, NULL, &result)
           == M_JOB_FUTURE_WAIT_OK);
    ok &= (result.status == M_JOB_RESULT_CANCELLED);
    m_job_future_deinit(&future);
    m_job_handle_destroy(job);
    m_job_queue_destroy(queue);
    return ok;
}

static bool run_test_future_timed_wait_timeout(void)
{
    m_job_queue_config_t config = M_JOB_QUEUE_CONFIG_DEFAULT;
    config.capacity = 2;
    config.worker_count = 1;

    m_job_queue_t *queue = m_job_queue_create(&config);
    if (queue == NULL) {
        return false;
    }

    m_job_handle_t *job = NULL;
    if (m_job_queue_submit_with_handle(queue,
                                       job_sleepy,
                                       NULL,
                                       &job)
        != M_JOB_OK) {
        m_job_queue_destroy(queue);
        return false;
    }

    m_job_future_t future = {0};
    bool ok = (m_job_future_init(&future, job) == M_JOB_OK);
    m_job_result_descriptor_t result = {0};
    ok &= (m_job_future_wait_timed(&future, 5000ULL, &result)
           == M_JOB_FUTURE_WAIT_TIMEOUT);
    m_job_future_deinit(&future);
    ok &= (m_job_wait_for_job(job, &result) == M_JOB_FUTURE_WAIT_OK);
    m_job_handle_destroy(job);
    m_job_queue_destroy(queue);
    return ok;
}

static bool run_test_future_try_not_ready(void)
{
    m_job_queue_config_t config = M_JOB_QUEUE_CONFIG_DEFAULT;
    config.capacity = 2;
    config.worker_count = 1;

    m_job_queue_t *queue = m_job_queue_create(&config);
    if (queue == NULL) {
        return false;
    }

    m_job_handle_t *job = NULL;
    if (m_job_queue_submit_with_handle(queue,
                                       job_sleepy,
                                       NULL,
                                       &job)
        != M_JOB_OK) {
        m_job_queue_destroy(queue);
        return false;
    }

    m_job_future_t future = {0};
    bool ok = (m_job_future_init(&future, job) == M_JOB_OK);
    m_job_result_descriptor_t result = {0};
    ok &= (m_job_future_try(&future, &result)
           == M_JOB_FUTURE_WAIT_NOT_READY);
    ok &= (m_job_wait_for_job(job, &result) == M_JOB_FUTURE_WAIT_OK);
    ok &= (m_job_future_try(&future, &result) == M_JOB_FUTURE_WAIT_OK);
    m_job_future_deinit(&future);
    m_job_handle_destroy(job);
    m_job_queue_destroy(queue);
    return ok;
}

static bool run_test_future_invalid_job(void)
{
    m_job_future_t future = {0};
    return (m_job_future_init(&future, NULL) == M_JOB_ERR_INVALID_PARAM);
}

static bool run_test_completion_wait_completed(void)
{
    m_job_queue_config_t config = M_JOB_QUEUE_CONFIG_DEFAULT;
    config.capacity = 2;
    config.worker_count = 1;

    m_job_queue_t *queue = m_job_queue_create(&config);
    if (queue == NULL) {
        return false;
    }

    m_job_handle_t *job = NULL;
    if (m_job_queue_submit_with_handle(queue,
                                       job_noop,
                                       NULL,
                                       &job)
        != M_JOB_OK) {
        m_job_queue_destroy(queue);
        return false;
    }

    m_job_result_descriptor_t first = {0};
    m_job_wait_for_job(job, &first);
    m_job_result_descriptor_t second = {0};
    bool ok = (m_job_wait_for_job(job, &second) == M_JOB_FUTURE_WAIT_OK);
    ok &= (second.status == M_JOB_RESULT_SUCCESS);
    m_job_handle_destroy(job);
    m_job_queue_destroy(queue);
    return ok;
}

static bool run_test_completion_wait_running(void)
{
    m_job_queue_config_t config = M_JOB_QUEUE_CONFIG_DEFAULT;
    config.capacity = 2;
    config.worker_count = 1;

    m_job_queue_t *queue = m_job_queue_create(&config);
    if (queue == NULL) {
        return false;
    }

    m_job_handle_t *job = NULL;
    if (m_job_queue_submit_with_handle(queue,
                                       job_sleepy,
                                       NULL,
                                       &job)
        != M_JOB_OK) {
        m_job_queue_destroy(queue);
        return false;
    }

    m_job_result_descriptor_t result = {0};
    bool ok = (m_job_wait_for_job(job, &result) == M_JOB_FUTURE_WAIT_OK);
    ok &= (result.status == M_JOB_RESULT_SUCCESS);
    m_job_handle_destroy(job);
    m_job_queue_destroy(queue);
    return ok;
}

static bool run_test_completion_timed_timeout(void)
{
    m_job_queue_config_t config = M_JOB_QUEUE_CONFIG_DEFAULT;
    config.capacity = 2;
    config.worker_count = 1;

    m_job_queue_t *queue = m_job_queue_create(&config);
    if (queue == NULL) {
        return false;
    }

    m_job_handle_t *job = NULL;
    if (m_job_queue_submit_with_handle(queue,
                                       job_sleepy,
                                       NULL,
                                       &job)
        != M_JOB_OK) {
        m_job_queue_destroy(queue);
        return false;
    }

    m_timer_deadline_t deadline = m_timer_deadline_from_relative(5000ULL);
    m_job_result_descriptor_t result = {0};
    bool ok = (m_job_wait_for_job_timed(job, &deadline, &result)
               == M_JOB_FUTURE_WAIT_TIMEOUT);
    ok &= (m_job_wait_for_job(job, &result) == M_JOB_FUTURE_WAIT_OK);
    m_job_handle_destroy(job);
    m_job_queue_destroy(queue);
    return ok;
}

static bool run_test_completion_cancelled(void)
{
    m_job_queue_config_t config = M_JOB_QUEUE_CONFIG_DEFAULT;
    config.capacity = 2;
    config.worker_count = 1;

    m_job_queue_t *queue = m_job_queue_create(&config);
    if (queue == NULL) {
        return false;
    }

    m_sched_task_id_t worker_id = m_job_queue_get_worker_task_id(queue, 0);
    bool suspended = (worker_id != M_SCHED_TASK_ID_INVALID)
                     && (m_sched_task_suspend(worker_id) == M_SCHED_OK);

    m_job_handle_t *job = NULL;
    if (m_job_queue_submit_with_handle(queue,
                                       job_sleepy,
                                       NULL,
                                       &job)
        != M_JOB_OK) {
        if (suspended) {
            m_sched_task_resume(worker_id);
        }
        m_job_queue_destroy(queue);
        return false;
    }

    bool cancelled = (m_job_cancel(job) == M_JOB_OK);
    if (suspended) {
        m_sched_task_resume(worker_id);
    }

    m_job_result_descriptor_t result = {0};
    bool ok = cancelled
              && (m_job_wait_for_job(job, &result) == M_JOB_FUTURE_WAIT_OK)
              && (result.status == M_JOB_RESULT_CANCELLED);
    m_job_handle_destroy(job);
    m_job_queue_destroy(queue);
    return ok;
}

void m_job_selftests_run(void)
{
    bool overall = true;
    overall &= test_report("job execution", run_test_job_execution());
    overall &= test_report("queue full handling", run_test_queue_full());
    overall &= test_report("timeout submission", run_test_timeout_submission());
    overall &= test_report("destroy while submitting",
                           run_test_destroy_while_submitting());
    overall &= test_report("job result success", run_test_job_result_success());
    overall &= test_report("job result error", run_test_job_result_error());
    overall &= test_report("job result cancelled",
                           run_test_job_result_cancelled());
    overall &= test_report("job result not ready",
                           run_test_job_result_not_ready());
    overall &= test_report("future wait success", run_test_future_wait_success());
    overall &= test_report("future wait error", run_test_future_wait_error());
    overall &= test_report("future wait cancelled",
                           run_test_future_wait_cancelled());
    overall &= test_report("future timed wait timeout",
                           run_test_future_timed_wait_timeout());
    overall &= test_report("future try not ready",
                           run_test_future_try_not_ready());
    overall &= test_report("future invalid job", run_test_future_invalid_job());
    overall &= test_report("completion wait completed",
                           run_test_completion_wait_completed());
    overall &= test_report("completion wait running",
                           run_test_completion_wait_running());
    overall &= test_report("completion timed timeout",
                           run_test_completion_timed_timeout());
    overall &= test_report("completion cancelled",
                           run_test_completion_cancelled());
    ESP_LOGI(TAG, "job self-tests %s", overall ? "PASSED" : "FAILED");
}

#else

#include "kernel/core/job/tests/m_job_tests.h"

#endif /* CONFIG_MAGNOLIA_JOB_SELFTESTS */
