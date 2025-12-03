/**
 * @file        m_job_result.c
 * @brief       Implementation of job result retrieval.
 */

#include "sdkconfig.h"
#include "freertos/portmacro.h"
#include "kernel/core/job/m_job_result.h"

#if CONFIG_MAGNOLIA_JOB_ENABLE_RESULTS

m_job_error_t m_job_query_result(m_job_id_t job,
                                 m_job_result_descriptor_t *result)
{
    if (job == NULL || result == NULL) {
        return M_JOB_ERR_INVALID_PARAM;
    }

    portENTER_CRITICAL(&job->lock);
    if (job->destroyed) {
        portEXIT_CRITICAL(&job->lock);
        return M_JOB_ERR_INVALID_HANDLE;
    }
    if (!job->result_ready) {
        portEXIT_CRITICAL(&job->lock);
        return M_JOB_ERR_NOT_READY;
    }
    *result = job->result;
    portEXIT_CRITICAL(&job->lock);
    return M_JOB_OK;
}

#else

m_job_error_t m_job_query_result(m_job_id_t job,
                                 m_job_result_descriptor_t *result)
{
    (void)job;
    (void)result;
    return M_JOB_ERR_STATE;
}

#endif
