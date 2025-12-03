/**
 * @file        m_job_worker.c
 * @brief       Worker task logic and scheduler hooks for the job subsystem.
 */

#include "sdkconfig.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "kernel/core/job/m_job_worker.h"
#include "kernel/core/job/m_job_queue.h"
#include "kernel/core/job/m_job_core.h"
#include "kernel/core/timer/m_timer.h"
#include "kernel/core/sched/m_sched.h"

static const char *TAG = "m_job";

/**
 * @brief   Scheduler hook triggered when a worker task starts running.
 */
static void m_job_worker_hook_start(m_sched_task_id_t id,
                                    m_sched_task_metadata_t *meta,
                                    void *user_data)
{
    (void)id;
    (void)user_data;

    m_job_queue_t *queue = (meta ? (m_job_queue_t *)meta->user_data : NULL);
    if (queue == NULL) {
        return;
    }

    _m_job_queue_lock(queue);
    ++queue->active_workers;
    if (queue->debug) {
        ESP_LOGD(TAG,
                 "worker %u started (active=%u)",
                 (unsigned)meta->id,
                 (unsigned)queue->active_workers);
    }
    _m_job_queue_unlock(queue);
}

/**
 * @brief   Scheduler hook triggered when a worker task stops running.
 */
static void m_job_worker_hook_stop(m_sched_task_id_t id,
                                   m_sched_task_metadata_t *meta,
                                   void *user_data)
{
    (void)id;
    (void)user_data;

    m_job_queue_t *queue = (meta ? (m_job_queue_t *)meta->user_data : NULL);
    if (queue == NULL) {
        return;
    }

    _m_job_queue_lock(queue);
    if (queue->active_workers > 0) {
        --queue->active_workers;
    }
    if (queue->debug) {
        ESP_LOGD(TAG,
                 "worker %u stopped (active=%u)",
                 (unsigned)meta->id,
                 (unsigned)queue->active_workers);
    }
    _m_job_queue_unlock(queue);
}

void m_job_worker_entry(void *arg)
{
    m_job_worker_t *worker = arg;
    if (worker == NULL || worker->queue == NULL) {
        return;
    }

    worker->task_handle = xTaskGetCurrentTaskHandle();
    m_job_queue_t *queue = worker->queue;

    while (true) {
        m_job_handle_t *job = NULL;
        m_job_error_t err = _m_job_queue_take(queue, &job, worker);
        if (err != M_JOB_OK) {
            break;
        }

        bool should_run = false;
        m_job_handler_result_t handler_result = {0};

        job_ctx_t *ctx = job->ctx;
        portENTER_CRITICAL(&job->lock);
        if (!job->cancelled && !job->result_ready) {
            job->state = M_JOB_STATE_RUNNING;
            should_run = true;
        }
        portEXIT_CRITICAL(&job->lock);

        if (should_run && ctx != NULL) {
            jctx_set_started(ctx, m_timer_get_monotonic());
            jctx_set_scheduler_state(ctx, JOB_CTX_SCHED_STATE_RUNNING);
            jctx_acquire(ctx);
            jctx_set_current(ctx);

            handler_result = job->handler(job, job->data);
            _m_job_queue_lock(queue);
            queue->stats.executed++;
            if (handler_result.status != M_JOB_RESULT_SUCCESS) {
                queue->stats.failed++;
            }
            _m_job_queue_unlock(queue);
            portENTER_CRITICAL(&job->lock);
            _m_job_handle_set_result(job, handler_result);
            portEXIT_CRITICAL(&job->lock);

            jctx_set_current(NULL);
            jctx_release(ctx);
        } else {
            portENTER_CRITICAL(&job->lock);
            _m_job_handle_record_cancellation(job);
            portEXIT_CRITICAL(&job->lock);
        }
    }
}

void m_job_worker_register_scheduler_hooks(void)
{
    static bool registered = false;
    if (registered) {
        return;
    }

    m_sched_worker_hooks_t hooks = {
        .on_worker_start = m_job_worker_hook_start,
        .on_worker_stop = m_job_worker_hook_stop,
    };
    m_sched_worker_hooks_register(&hooks);
    registered = true;
}
