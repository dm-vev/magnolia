/**
 * @file        m_job_future.h
 * @brief       Future-based waiting helpers for job completion.
 */
#ifndef MAGNOLIA_JOB_M_JOB_FUTURE_H
#define MAGNOLIA_JOB_M_JOB_FUTURE_H

#include <stdbool.h>
#include <stdint.h>

#include "sdkconfig.h"
#include "kernel/core/ipc/ipc_scheduler_bridge.h"
#include "kernel/core/job/m_job_core.h"
#include "kernel/core/timer/m_timer.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief   Result codes returned by future wait helpers.
 */
typedef enum {
    M_JOB_FUTURE_WAIT_OK = 0,
    M_JOB_FUTURE_WAIT_TIMEOUT,
    M_JOB_FUTURE_WAIT_NOT_READY,
    M_JOB_FUTURE_WAIT_DESTROYED,
    M_JOB_FUTURE_WAIT_SHUTDOWN,
} m_job_future_wait_result_t;

/**
 * @brief   Future object used to await job completion.
 */
typedef struct m_job_future {
    m_job_id_t job;
    ipc_waiter_t waiter;
    bool initialized;
    bool waiting;
} m_job_future_t;

/**
 * @brief   Initialize a future for a job handle.
 */
m_job_error_t m_job_future_init(m_job_future_t *future, m_job_id_t job);

/**
 * @brief   Deinitialize a previously initialized future.
 */
void m_job_future_deinit(m_job_future_t *future);

/**
 * @brief   Wait until a job completes.
 */
m_job_future_wait_result_t m_job_future_wait(m_job_future_t *future,
                                             const m_timer_deadline_t *deadline,
                                             m_job_result_descriptor_t *result);

/**
 * @brief   Wait until a job completes or the relative timeout expires.
 */
m_job_future_wait_result_t m_job_future_wait_timed(m_job_future_t *future,
                                                   uint64_t timeout_us,
                                                   m_job_result_descriptor_t *result);

/**
 * @brief   Try to wait on a future without blocking.
 */
m_job_future_wait_result_t m_job_future_try(m_job_future_t *future,
                                            m_job_result_descriptor_t *result);

#ifdef __cplusplus
}
#endif

#endif /* MAGNOLIA_JOB_M_JOB_FUTURE_H */
