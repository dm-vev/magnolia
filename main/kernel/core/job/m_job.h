/*
 * Magnolia OS — Job Subsystem
 * Purpose:
 *     Lightweight job queue over the Magnolia scheduler abstraction.
 *
 * © 2025 Magnolia Project
 */

#ifndef MAGNOLIA_JOB_M_JOB_H
#define MAGNOLIA_JOB_M_JOB_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "kernel/core/sched/m_sched.h"
#include "kernel/core/timer/m_timer.h"

#define M_JOB_QUEUE_NAME_MAX_LEN 24

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    M_JOB_OK = 0,
    M_JOB_ERR_INVALID_PARAM,
    M_JOB_ERR_NO_MEMORY,
    M_JOB_ERR_QUEUE_FULL,
    M_JOB_ERR_TIMEOUT,
    M_JOB_ERR_DESTROYED,
    M_JOB_ERR_SHUTDOWN,
} m_job_error_t;

typedef bool (*m_job_handler_t)(void *data);

typedef struct {
    uint32_t submitted;
    uint32_t executed;
    uint32_t failed;
    uint32_t dropped;
} m_job_stats_t;

typedef struct {
    size_t depth;
    size_t capacity;
    size_t worker_count;
    size_t active_workers;
    bool shutdown;
    bool destroyed;
} m_job_queue_info_t;

typedef struct {
    const char *name;
    size_t capacity;
    size_t worker_count;
    size_t stack_depth;
    UBaseType_t priority;
    bool debug_log;
} m_job_queue_config_t;

typedef struct m_job_queue m_job_queue_t;

#define M_JOB_QUEUE_CONFIG_DEFAULT                                             \
    {                                                                          \
        .name = "job_queue",                                                   \
        .capacity = 8,                                                         \
        .worker_count = 1,                                                     \
        .stack_depth = configMINIMAL_STACK_SIZE,                              \
        .priority = (tskIDLE_PRIORITY + 1),                                   \
        .debug_log = false,                                                    \
    }

m_job_queue_t *m_job_queue_create(const m_job_queue_config_t *config);
m_job_error_t m_job_queue_destroy(m_job_queue_t *queue);
m_job_error_t m_job_queue_submit(m_job_queue_t *queue,
                                 m_job_handler_t handler,
                                 void *data);
m_job_error_t m_job_queue_submit_nowait(m_job_queue_t *queue,
                                        m_job_handler_t handler,
                                        void *data);
m_job_error_t m_job_queue_submit_until(m_job_queue_t *queue,
                                       m_job_handler_t handler,
                                       void *data,
                                       const m_timer_deadline_t *deadline);

void m_job_queue_get_info(const m_job_queue_t *queue,
                          m_job_queue_info_t *info);
void m_job_queue_get_stats(const m_job_queue_t *queue, m_job_stats_t *stats);

#ifdef __cplusplus
}
#endif

#endif /* MAGNOLIA_JOB_M_JOB_H */
