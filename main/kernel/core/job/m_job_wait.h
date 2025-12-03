/**
 * @file        m_job_wait.h
 * @brief       Thin wait helpers built on job futures.
 */
#ifndef MAGNOLIA_JOB_M_JOB_WAIT_H
#define MAGNOLIA_JOB_M_JOB_WAIT_H

#include "sdkconfig.h"
#include "kernel/core/job/m_job_future.h"
#include "kernel/core/timer/m_timer.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief   Wait indefinitely for a job to complete.
 */
m_job_future_wait_result_t m_job_wait_for_job(m_job_id_t job,
                                               m_job_result_descriptor_t *result);

/**
 * @brief   Wait until the provided deadline for a job to complete.
 */
m_job_future_wait_result_t m_job_wait_for_job_timed(m_job_id_t job,
                                                    const m_timer_deadline_t *deadline,
                                                    m_job_result_descriptor_t *result);

/**
 * @brief   Poll a job for completion without blocking.
 */
m_job_future_wait_result_t m_job_try_wait_for_job(m_job_id_t job,
                                                   m_job_result_descriptor_t *result);

#ifdef __cplusplus
}
#endif

#endif /* MAGNOLIA_JOB_M_JOB_WAIT_H */
