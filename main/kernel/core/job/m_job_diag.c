/**
 * @file        m_job_diag.c
 * @brief       Diagnostics for job handles and futures.
 */

#include "sdkconfig.h"
#include "freertos/portmacro.h"
#include "kernel/core/job/m_job_diag.h"
#include "kernel/core/job/jctx.h"

m_job_error_t m_job_diag_info(m_job_id_t job, m_job_diag_info_t *info)
{
    if (job == NULL || info == NULL) {
        return M_JOB_ERR_INVALID_PARAM;
    }

    portENTER_CRITICAL(&job->lock);
    if (job->destroyed) {
        portEXIT_CRITICAL(&job->lock);
        return M_JOB_ERR_INVALID_HANDLE;
    }
    info->completed = job->result_ready;
    info->result_status = job->result.status;
    info->has_payload = (job->result.payload != NULL
                         && job->result.payload_size > 0);
    info->payload_size = job->result.payload_size;
    info->attached_futures = job->future_count;
    if (job->ctx != NULL) {
        m_timer_time_t value = 0;
        if (jctx_get_field_kernel(job->ctx,
                                  JOB_CTX_FIELD_SUBMITTED_AT,
                                  &value,
                                  sizeof(value)) == JOB_CTX_OK) {
            info->submitted_at = value;
        }
        value = 0;
        if (jctx_get_field_kernel(job->ctx,
                                  JOB_CTX_FIELD_STARTED_AT,
                                  &value,
                                  sizeof(value)) == JOB_CTX_OK) {
            info->started_at = value;
        }
        value = 0;
        if (jctx_get_field_kernel(job->ctx,
                                  JOB_CTX_FIELD_COMPLETED_AT,
                                  &value,
                                  sizeof(value)) == JOB_CTX_OK) {
            info->completed_at = value;
        }
    } else {
        info->submitted_at = 0;
        info->started_at = 0;
        info->completed_at = 0;
    }
    portEXIT_CRITICAL(&job->lock);
    return M_JOB_OK;
}

m_job_error_t m_job_future_diag(const m_job_future_t *future,
                                m_job_future_diag_info_t *info)
{
    if (future == NULL || info == NULL || !future->initialized) {
        return M_JOB_ERR_INVALID_PARAM;
    }

    if (future->job == NULL) {
        return M_JOB_ERR_INVALID_HANDLE;
    }

    portENTER_CRITICAL(&future->job->lock);
    info->job = future->job;
    info->waiters = future->job->waiter_count;
    portEXIT_CRITICAL(&future->job->lock);
    return M_JOB_OK;
}
