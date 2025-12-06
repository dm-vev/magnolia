/**
 * @file        kernel/core/job/m_job_event.h
 * @brief       Job lifecycle event exports used by dependent subsystems.
 */
#ifndef MAGNOLIA_JOB_M_JOB_EVENT_H
#define MAGNOLIA_JOB_M_JOB_EVENT_H

#include "sdkconfig.h"
#include "kernel/core/job/m_job_core.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*m_job_destroy_callback_t)(m_job_id_t job,
                                         void *user_data);

m_job_error_t m_job_subscribe_destroy(m_job_destroy_callback_t callback,
                                      void *user_data);

void _m_job_notify_destroyed(m_job_id_t job);

#ifdef __cplusplus
}
#endif

#endif /* MAGNOLIA_JOB_M_JOB_EVENT_H */
