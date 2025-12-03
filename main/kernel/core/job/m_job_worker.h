/**
 * @file        m_job_worker.h
 * @brief       Worker thread state and entry point declaration.
 */
#ifndef MAGNOLIA_JOB_M_JOB_WORKER_H
#define MAGNOLIA_JOB_M_JOB_WORKER_H

#include <stdbool.h>

#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "kernel/core/sched/m_sched.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct m_job_queue m_job_queue_t;

typedef struct m_job_worker {
    m_job_queue_t *queue;
    m_sched_wait_context_t wait;
    struct m_job_worker *next_waiter;
    struct m_job_worker *prev_waiter;
    bool waiting;
    m_sched_task_id_t task_id;
    TaskHandle_t task_handle;
} m_job_worker_t;

/**
 * @brief   Worker entry point executed by Magnolia scheduler tasks.
 */
void m_job_worker_entry(void *arg);

/**
 * @brief   Register Magnolia scheduler hooks for worker lifecycle tracing.
 */
void m_job_worker_register_scheduler_hooks(void);

#ifdef __cplusplus
}
#endif

#endif /* MAGNOLIA_JOB_M_JOB_WORKER_H */
