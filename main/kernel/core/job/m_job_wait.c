/**
 * @file        m_job_wait.c
 * @brief       Unified wait helpers built on job futures.
 */

#include "sdkconfig.h"
#include "kernel/core/job/m_job_wait.h"
#include "kernel/core/job/m_job_future.h"

#if CONFIG_MAGNOLIA_JOB_ENABLE_FUTURES

/**
 * @brief   Helper that waits on a job by temporarily creating a future.
 */
m_job_future_wait_result_t m_job_wait_for_job_internal(
        m_job_id_t job,
        const m_timer_deadline_t *deadline,
        m_job_result_descriptor_t *result)
{
    if (job == NULL) {
        return M_JOB_FUTURE_WAIT_DESTROYED;
    }

    m_job_future_t future = {0};
    if (m_job_future_init(&future, job) != M_JOB_OK) {
        return M_JOB_FUTURE_WAIT_DESTROYED;
    }

    m_job_future_wait_result_t wait_result =
            m_job_future_wait(&future, deadline, result);
    m_job_future_deinit(&future);
    return wait_result;
}

m_job_future_wait_result_t m_job_wait_for_job(m_job_id_t job,
                                               m_job_result_descriptor_t *result)
{
    return m_job_wait_for_job_internal(job, NULL, result);
}

m_job_future_wait_result_t m_job_wait_for_job_timed(m_job_id_t job,
                                                    const m_timer_deadline_t *deadline,
                                                    m_job_result_descriptor_t *result)
{
    return m_job_wait_for_job_internal(job, deadline, result);
}

m_job_future_wait_result_t m_job_try_wait_for_job(m_job_id_t job,
                                                   m_job_result_descriptor_t *result)
{
    m_job_future_t future = {0};
    if (m_job_future_init(&future, job) != M_JOB_OK) {
        return M_JOB_FUTURE_WAIT_DESTROYED;
    }
    m_job_future_wait_result_t wait_result =
            m_job_future_try(&future, result);
    m_job_future_deinit(&future);
    return wait_result;
}

#else

m_job_future_wait_result_t m_job_wait_for_job(m_job_id_t job,
                                               m_job_result_descriptor_t *result)
{
    (void)job;
    (void)result;
    return M_JOB_FUTURE_WAIT_DESTROYED;
}

m_job_future_wait_result_t m_job_wait_for_job_timed(m_job_id_t job,
                                                    const m_timer_deadline_t *deadline,
                                                    m_job_result_descriptor_t *result)
{
    (void)job;
    (void)deadline;
    (void)result;
    return M_JOB_FUTURE_WAIT_DESTROYED;
}

m_job_future_wait_result_t m_job_try_wait_for_job(m_job_id_t job,
                                                   m_job_result_descriptor_t *result)
{
    (void)job;
    (void)result;
    return M_JOB_FUTURE_WAIT_DESTROYED;
}

#endif
