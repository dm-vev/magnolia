/**
 * @file        m_job_core.c
 * @brief       Job handle lifecycle and metadata implementation.
 * @details     Implements allocation, destruction, result storage, cancellation, and
 *              scheduler-context helpers for Magnolia jobs.
 */

#include <string.h>

#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "kernel/core/job/m_job_core.h"
#include "kernel/core/job/jctx.h"
#include "kernel/core/timer/m_timer.h"

/**
 * @brief   Zero-initialize a job handle before submission.
 */
static void _m_job_handle_init(m_job_handle_t *handle,
                               m_job_handler_t handler,
                               void *data)
{
    memset(handle, 0, sizeof(*handle));
    handle->handler = handler;
    handle->data = data;
    handle->state = M_JOB_STATE_PENDING;
    handle->lock = (portMUX_TYPE)portMUX_INITIALIZER_UNLOCKED;
    ipc_wait_queue_init(&handle->waiters);
}

void _m_job_handle_set_result(m_job_handle_t *handle,
                              m_job_handler_result_t result)
{
    if (handle->result_ready || handle->destroyed) {
        return;
    }

    handle->result = result;
    handle->result_ready = true;
    handle->state = M_JOB_STATE_COMPLETED;
    if (handle->ctx != NULL) {
        jctx_set_completed(handle->ctx, m_timer_get_monotonic());
        jctx_set_scheduler_state(handle->ctx, JOB_CTX_SCHED_STATE_COMPLETED);
    }
    ipc_wake_all(&handle->waiters, IPC_WAIT_RESULT_OK);
}

void _m_job_handle_record_cancellation(m_job_handle_t *handle)
{
    if (handle->result_ready || handle->destroyed) {
        return;
    }

    handle->cancelled = true;
    handle->result.status = M_JOB_RESULT_CANCELLED;
    handle->result.payload = NULL;
    handle->result.payload_size = 0;
    handle->result_ready = true;
    handle->state = M_JOB_STATE_COMPLETED;
    if (handle->ctx != NULL) {
        jctx_mark_cancelled(handle->ctx);
        jctx_set_completed(handle->ctx, m_timer_get_monotonic());
    }
    ipc_wake_all(&handle->waiters, IPC_WAIT_RESULT_OK);
}

m_job_handle_t *_m_job_handle_create(m_job_handler_t handler,
                                      void *data,
                                      m_job_id_t parent_job)
{
    m_job_handle_t *handle = pvPortMalloc(sizeof(*handle));
    if (handle == NULL) {
        return NULL;
    }

    _m_job_handle_init(handle, handler, data);
    handle->ctx = jctx_create(handle, parent_job);
    if (handle->ctx == NULL) {
        vPortFree(handle);
        return NULL;
    }
    handle->result.status = M_JOB_RESULT_ERROR;
    return handle;
}

m_job_error_t m_job_cancel(m_job_id_t job)
{
#if CONFIG_MAGNOLIA_JOB_ENABLE_CANCELLATION
    if (job == NULL) {
        return M_JOB_ERR_INVALID_HANDLE;
    }

    portENTER_CRITICAL(&job->lock);
    if (job->result_ready || job->destroyed) {
        portEXIT_CRITICAL(&job->lock);
        return M_JOB_ERR_STATE;
    }
    _m_job_handle_record_cancellation(job);
    portEXIT_CRITICAL(&job->lock);
    return M_JOB_OK;
#else
    (void)job;
    return M_JOB_ERR_STATE;
#endif
}

m_job_error_t m_job_handle_destroy(m_job_id_t job)
{
    if (job == NULL) {
        return M_JOB_ERR_INVALID_HANDLE;
    }

    portENTER_CRITICAL(&job->lock);
    if (job->destroyed) {
        portEXIT_CRITICAL(&job->lock);
        return M_JOB_ERR_DESTROYED;
    }
    if (!job->result_ready) {
        portEXIT_CRITICAL(&job->lock);
        return M_JOB_ERR_NOT_READY;
    }
    if (job->future_count > 0) {
        portEXIT_CRITICAL(&job->lock);
        return M_JOB_ERR_BUSY;
    }
    job->destroyed = true;
    portEXIT_CRITICAL(&job->lock);
    if (job->ctx != NULL) {
        jctx_release(job->ctx);
        job->ctx = NULL;
    }
    vPortFree(job);
    return M_JOB_OK;
}

job_ctx_error_t m_job_field_get(m_job_id_t job,
                                job_ctx_field_id_t field,
                                void *out_buf,
                                size_t buf_size)
{
    if (job == NULL || out_buf == NULL) {
        return JOB_CTX_ERR_INVALID_PARAM;
    }

    portENTER_CRITICAL(&job->lock);
    if (job->destroyed || job->ctx == NULL) {
        portEXIT_CRITICAL(&job->lock);
        return JOB_CTX_ERR_INVALID_FIELD;
    }

    job_ctx_t *ctx = job->ctx;
    jctx_acquire(ctx);
    portEXIT_CRITICAL(&job->lock);

    job_ctx_field_policy_t policy = jctx_field_policy(field);
    if (policy == JOB_CTX_FIELD_POLICY_PRIVATE) {
        jctx_release(ctx);
        return JOB_CTX_ERR_NO_PERMISSION;
    }

    job_ctx_error_t err = jctx_get_field_kernel(ctx, field, out_buf, buf_size);
    jctx_release(ctx);
    return err;
}

job_ctx_error_t m_job_field_set(m_job_id_t job,
                                job_ctx_field_id_t field,
                                const void *value,
                                size_t value_size)
{
    if (job == NULL) {
        return JOB_CTX_ERR_INVALID_PARAM;
    }

    portENTER_CRITICAL(&job->lock);
    if (job->destroyed || job->ctx == NULL) {
        portEXIT_CRITICAL(&job->lock);
        return JOB_CTX_ERR_INVALID_FIELD;
    }

    job_ctx_t *ctx = job->ctx;
    jctx_acquire(ctx);
    portEXIT_CRITICAL(&job->lock);

    job_ctx_field_policy_t policy = jctx_field_policy(field);
    if (policy != JOB_CTX_FIELD_POLICY_PUBLIC) {
        jctx_release(ctx);
        return JOB_CTX_ERR_NO_PERMISSION;
    }

    job_ctx_t *current = jctx_current();
    if (current == NULL || current->job_id != job) {
        jctx_release(ctx);
        return JOB_CTX_ERR_NO_PERMISSION;
    }

    job_ctx_error_t err = jctx_set_field_kernel(ctx, field, value, value_size);
    jctx_release(ctx);
    return err;
}
