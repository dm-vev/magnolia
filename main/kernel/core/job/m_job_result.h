/**
 * @file        m_job_result.h
 * @brief       Job result helpers and query API.
 */
#ifndef MAGNOLIA_JOB_M_JOB_RESULT_H
#define MAGNOLIA_JOB_M_JOB_RESULT_H

#include "sdkconfig.h"
#include "kernel/core/job/m_job_core.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief   Build a success descriptor referencing the provided payload.
 */
static inline m_job_result_descriptor_t m_job_result_success(const void *payload,
                                                             size_t payload_size)
{
    return (m_job_result_descriptor_t){
        .status = M_JOB_RESULT_SUCCESS,
        .payload = payload,
        .payload_size = payload_size,
    };
}

/**
 * @brief   Build an error descriptor referencing the provided payload.
 */
static inline m_job_result_descriptor_t m_job_result_error(const void *payload,
                                                           size_t payload_size)
{
    return (m_job_result_descriptor_t){
        .status = M_JOB_RESULT_ERROR,
        .payload = payload,
        .payload_size = payload_size,
    };
}

/**
 * @brief   Query the result descriptor of a completed job.
 */
m_job_error_t m_job_query_result(m_job_id_t job,
                                 m_job_result_descriptor_t *result);

#ifdef __cplusplus
}
#endif

#endif /* MAGNOLIA_JOB_M_JOB_RESULT_H */
