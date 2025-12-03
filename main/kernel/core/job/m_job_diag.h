/**
 * @file        m_job_diag.h
 * @brief       Diagnostic helpers for jobs and futures.
 */
#ifndef MAGNOLIA_JOB_M_JOB_DIAG_H
#define MAGNOLIA_JOB_M_JOB_DIAG_H

#include <stddef.h>

#include "sdkconfig.h"
#include "kernel/core/job/m_job_core.h"
#include "kernel/core/job/m_job_future.h"
#include "kernel/core/timer/m_timer.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief   Diagnostic snapshot describing a job handle.
 */
typedef struct {
    bool completed;
    m_job_result_status_t result_status;
    bool has_payload;
    size_t payload_size;
    size_t attached_futures;
    m_timer_time_t submitted_at;
    m_timer_time_t started_at;
    m_timer_time_t completed_at;
} m_job_diag_info_t;

/**
 * @brief   Diagnostic snapshot describing a job future.
 */
typedef struct {
    m_job_id_t job;
    size_t waiters;
} m_job_future_diag_info_t;

/**
 * @brief   Retrieve diagnostic information for a job handle.
 */
m_job_error_t m_job_diag_info(m_job_id_t job, m_job_diag_info_t *info);

/**
 * @brief   Retrieve diagnostic information for a job future.
 */
m_job_error_t m_job_future_diag(const m_job_future_t *future,
                                m_job_future_diag_info_t *info);

#ifdef __cplusplus
}
#endif

#endif /* MAGNOLIA_JOB_M_JOB_DIAG_H */
