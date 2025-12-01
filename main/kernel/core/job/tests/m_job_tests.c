#include "sdkconfig.h"

#ifdef CONFIG_MAGNOLIA_JOB_SELFTESTS

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

static bool job_increment(void *arg)
{
    job_test_context_t *ctx = arg;
    ctx->count++;
    xSemaphoreGive(ctx->done);
    return true;
}

static bool job_noop(void *arg)
{
    (void)arg;
    return true;
}

static bool job_sleepy(void *arg)
{
    (void)arg;
    m_sched_sleep_ms(50);
    return true;
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

    StaticSemaphore_t storage;
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

    m_sched_task_id_t worker_id = queue->workers[0].task_id;
    bool suspended = (m_sched_task_suspend(worker_id) == M_SCHED_OK);
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

    m_sched_task_id_t worker_id = queue->workers[0].task_id;
    bool suspended = (m_sched_task_suspend(worker_id) == M_SCHED_OK);
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

static m_job_error_t g_block_submit_result = M_JOB_OK;
static SemaphoreHandle_t g_block_submit_done = NULL;

static void job_blocking_submitter(void *arg)
{
    m_job_queue_t *queue = arg;
    g_block_submit_result = m_job_queue_submit(queue, job_noop, NULL);
    xSemaphoreGive(g_block_submit_done);
    vTaskDelete(NULL);
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

    m_sched_task_id_t worker_id = queue->workers[0].task_id;
    bool suspended = (m_sched_task_suspend(worker_id) == M_SCHED_OK);

    if (m_job_queue_submit(queue, job_sleepy, NULL) != M_JOB_OK) {
        if (suspended) {
            m_sched_task_resume(worker_id);
        }
        m_job_queue_destroy(queue);
        return false;
    }

    StaticSemaphore_t storage;
    g_block_submit_done = xSemaphoreCreateBinaryStatic(&storage);
    if (g_block_submit_done == NULL) {
        m_job_queue_destroy(queue);
        return false;
    }

    BaseType_t created = xTaskCreate(job_blocking_submitter,
                                     "job_blocker",
                                     configMINIMAL_STACK_SIZE,
                                     queue,
                                     (tskIDLE_PRIORITY + 2),
                                     NULL);
    if (created != pdPASS) {
        m_job_queue_destroy(queue);
        return false;
    }

    m_sched_sleep_ms(5);

    if (suspended) {
        m_sched_task_resume(worker_id);
    }

    m_job_error_t destroy_res = m_job_queue_destroy(queue);
    bool ok = (destroy_res == M_JOB_OK);
    if (xSemaphoreTake(g_block_submit_done, pdMS_TO_TICKS(1000)) == pdTRUE) {
        ok &= (g_block_submit_result == M_JOB_ERR_DESTROYED);
    } else {
        ok = false;
    }

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
    ESP_LOGI(TAG, "job self-tests %s", overall ? "PASSED" : "FAILED");
}

#else

#include "kernel/core/job/tests/m_job_tests.h"


#endif /* CONFIG_MAGNOLIA_JOB_SELFTESTS */
