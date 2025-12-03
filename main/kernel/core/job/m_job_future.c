/**
 * @file        m_job_future.c
 * @brief       Job future lifecycle implementation.
 */

#include "sdkconfig.h"
#include "freertos/portmacro.h"
#include "kernel/core/job/m_job_future.h"
#include "kernel/core/job/m_job_core.h"
#include "kernel/core/sched/m_sched.h"

/**
 * @brief   Convert IPC wait results into job future wait codes.
 */
static m_job_future_wait_result_t m_job_future_result_from_wait(
        ipc_wait_result_t wait_result)
{
    switch (wait_result) {
    case IPC_WAIT_RESULT_OK:
        return M_JOB_FUTURE_WAIT_OK;
    case IPC_WAIT_RESULT_TIMEOUT:
        return M_JOB_FUTURE_WAIT_TIMEOUT;
    case IPC_WAIT_RESULT_OBJECT_DESTROYED:
        return M_JOB_FUTURE_WAIT_DESTROYED;
    default:
        return M_JOB_FUTURE_WAIT_SHUTDOWN;
    }
}

/**
 * @brief   Common waiting logic shared by timed, untimed, and try waits.
 */
static m_job_future_wait_result_t m_job_future_wait_common(
        m_job_future_t *future,
        const m_timer_deadline_t *deadline,
        m_job_result_descriptor_t *result)
{
    if (future == NULL || !future->initialized || future->job == NULL) {
        return M_JOB_FUTURE_WAIT_DESTROYED;
    }

    m_job_id_t job = future->job;

    portENTER_CRITICAL(&job->lock);
    if (job->destroyed) {
        portEXIT_CRITICAL(&job->lock);
        return M_JOB_FUTURE_WAIT_DESTROYED;
    }

    if (job->result_ready) {
        if (result != NULL) {
            *result = job->result;
        }
        portEXIT_CRITICAL(&job->lock);
        return M_JOB_FUTURE_WAIT_OK;
    }

    if (future->waiting) {
        portEXIT_CRITICAL(&job->lock);
        return M_JOB_FUTURE_WAIT_SHUTDOWN;
    }

    future->waiting = true;
    future->waiter.enqueued = false;
    ipc_waiter_prepare(&future->waiter, M_SCHED_WAIT_REASON_JOB);
    ipc_waiter_enqueue(&job->waiters, &future->waiter);
    ++job->waiter_count;
    portEXIT_CRITICAL(&job->lock);

    ipc_wait_result_t wait_res = ipc_waiter_block(&future->waiter, deadline);

    portENTER_CRITICAL(&job->lock);
    future->waiting = false;
    if (job->waiter_count > 0) {
        --job->waiter_count;
    }
    ipc_waiter_remove(&job->waiters, &future->waiter);

    m_job_future_wait_result_t wait_result =
            m_job_future_result_from_wait(wait_res);

    if (wait_result == M_JOB_FUTURE_WAIT_OK && job->result_ready) {
        if (result != NULL) {
            *result = job->result;
        }
        portEXIT_CRITICAL(&job->lock);
        return M_JOB_FUTURE_WAIT_OK;
    }

    portEXIT_CRITICAL(&job->lock);
    return wait_result;
}

m_job_error_t m_job_future_init(m_job_future_t *future, m_job_id_t job)
{
    if (future == NULL || job == NULL) {
        return M_JOB_ERR_INVALID_PARAM;
    }

    portENTER_CRITICAL(&job->lock);
    if (job->destroyed) {
        portEXIT_CRITICAL(&job->lock);
        return M_JOB_ERR_DESTROYED;
    }
    ++job->future_count;
    future->job = job;
    future->initialized = true;
    future->waiting = false;
    m_sched_wait_context_prepare_with_reason(&future->waiter.ctx,
                                             M_SCHED_WAIT_REASON_JOB);
    portEXIT_CRITICAL(&job->lock);
    return M_JOB_OK;
}

void m_job_future_deinit(m_job_future_t *future)
{
    if (future == NULL || !future->initialized) {
        return;
    }

    if (future->job != NULL) {
        portENTER_CRITICAL(&future->job->lock);
        if (future->job->future_count > 0) {
            future->job->future_count--;
        }
        portEXIT_CRITICAL(&future->job->lock);
    }

    future->initialized = false;
    future->waiting = false;
    future->job = NULL;
}

m_job_future_wait_result_t m_job_future_wait(m_job_future_t *future,
                                             const m_timer_deadline_t *deadline,
                                             m_job_result_descriptor_t *result)
{
    return m_job_future_wait_common(future, deadline, result);
}

m_job_future_wait_result_t m_job_future_wait_timed(m_job_future_t *future,
                                                   uint64_t timeout_us,
                                                   m_job_result_descriptor_t *result)
{
    m_timer_deadline_t deadline = m_timer_deadline_from_relative(timeout_us);
    return m_job_future_wait_common(future, &deadline, result);
}

m_job_future_wait_result_t m_job_future_try(m_job_future_t *future,
                                            m_job_result_descriptor_t *result)
{
    if (future == NULL || !future->initialized || future->job == NULL) {
        return M_JOB_FUTURE_WAIT_DESTROYED;
    }

    m_job_id_t job = future->job;
    portENTER_CRITICAL(&job->lock);
    if (job->destroyed) {
        portEXIT_CRITICAL(&job->lock);
        return M_JOB_FUTURE_WAIT_DESTROYED;
    }

    if (job->result_ready) {
        if (result != NULL) {
            *result = job->result;
        }
        portEXIT_CRITICAL(&job->lock);
        return M_JOB_FUTURE_WAIT_OK;
    }

    portEXIT_CRITICAL(&job->lock);
    return M_JOB_FUTURE_WAIT_NOT_READY;
}
