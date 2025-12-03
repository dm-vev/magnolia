/**
 * @file        m_job_queue.h
 * @brief       Job queue implementation and submission API.
 * @details     Provides enqueue/dequeue semantics, capacity control, blocking submission,
 *              and runtime statistics for Magnolia job queues.
 */
#ifndef MAGNOLIA_JOB_M_JOB_QUEUE_H
#define MAGNOLIA_JOB_M_JOB_QUEUE_H

#include <stdbool.h>
#include <stddef.h>

#include "sdkconfig.h"
#ifndef CONFIG_MAGNOLIA_JOB_ENABLE_EXTENDED_DIAGNOSTICS
#define CONFIG_MAGNOLIA_JOB_ENABLE_EXTENDED_DIAGNOSTICS 0
#endif
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "kernel/core/sched/m_sched.h"
#include "kernel/core/timer/m_timer.h"
#include "kernel/core/job/m_job_core.h"
#include "kernel/core/job/m_job_worker.h"

#ifdef __cplusplus
extern "C" {
#endif

#define M_JOB_QUEUE_NAME_MAX_LEN CONFIG_MAGNOLIA_JOB_QUEUE_NAME_MAX_LEN

/**
 * @brief   Cumulative statistics emitted by the queue.
 */
typedef struct {
    size_t submitted;
    size_t executed;
    size_t failed;
    size_t dropped;
} m_job_stats_t;

/**
 * @brief   Snapshot of queue depth and worker state.
 */
typedef struct {
    size_t depth;
    size_t capacity;
    size_t worker_count;
    size_t active_workers;
    bool shutdown;
    bool destroyed;
} m_job_queue_info_t;

/**
 * @brief   Configuration parameters used when creating a job queue.
 */
typedef struct {
    const char *name;
    size_t capacity;
    size_t worker_count;
    size_t stack_depth;
    UBaseType_t priority;
    bool debug_log;
} m_job_queue_config_t;

/**
 * @brief   Default job queue configuration.
 */
#define M_JOB_QUEUE_CONFIG_DEFAULT                                             \
    {                                                                          \
        .name = "job_queue",                                                   \
        .capacity = CONFIG_MAGNOLIA_JOB_QUEUE_DEFAULT_CAPACITY,                \
        .worker_count = CONFIG_MAGNOLIA_JOB_QUEUE_DEFAULT_WORKER_COUNT,        \
        .stack_depth = CONFIG_MAGNOLIA_JOB_WORKER_STACK_DEPTH,                 \
        .priority = CONFIG_MAGNOLIA_JOB_WORKER_PRIORITY,                       \
        .debug_log = CONFIG_MAGNOLIA_JOB_ENABLE_EXTENDED_DIAGNOSTICS,          \
    }

typedef struct m_job_queue m_job_queue_t;

typedef struct m_job_submit_wait_node {
    m_sched_wait_context_t ctx;
    struct m_job_submit_wait_node *next;
    bool linked;
} m_job_submit_wait_node_t;

struct m_job_queue {
    char name[M_JOB_QUEUE_NAME_MAX_LEN];
    size_t capacity;
    size_t head;
    size_t tail;
    size_t count;
    m_job_handle_t **ring;
    m_job_worker_t *workers;
    size_t worker_count;
    UBaseType_t worker_priority;
    SemaphoreHandle_t lock;
    StaticSemaphore_t lock_storage;
    m_job_worker_t *worker_waiters_head;
    m_job_worker_t *worker_waiters_tail;
    m_job_submit_wait_node_t *submit_waiters_head;
    m_job_submit_wait_node_t *submit_waiters_tail;
    m_job_stats_t stats;
    bool destroyed;
    bool shutdown_requested;
    bool debug;
    size_t active_workers;
};

/**
 * @brief   Internal helpers for queue locking.
 */
static inline void _m_job_queue_lock(m_job_queue_t *queue)
{
    xSemaphoreTake(queue->lock, portMAX_DELAY);
}

static inline void _m_job_queue_unlock(m_job_queue_t *queue)
{
    xSemaphoreGive(queue->lock);
}

/**
 * @brief   Create a worker queue that executes Magnolia job handlers.
 */
m_job_queue_t *m_job_queue_create(const m_job_queue_config_t *config);

/**
 * @brief   Destroy a job queue instance.
 */
m_job_error_t m_job_queue_destroy(m_job_queue_t *queue);

/**
 * @brief   Submit a job handler and optionally obtain the job handle.
 */
m_job_error_t m_job_queue_submit_with_handle(m_job_queue_t *queue,
                                             m_job_handler_t handler,
                                             void *data,
                                             m_job_handle_t **out_handle);

static inline m_job_error_t m_job_queue_submit(m_job_queue_t *queue,
                                               m_job_handler_t handler,
                                               void *data)
{
    return m_job_queue_submit_with_handle(queue, handler, data, NULL);
}

/**
 * @brief   Submit a job handler without blocking when the queue is full.
 */
m_job_error_t m_job_queue_submit_nowait_with_handle(m_job_queue_t *queue,
                                                    m_job_handler_t handler,
                                                    void *data,
                                                    m_job_handle_t **out_handle);

static inline m_job_error_t m_job_queue_submit_nowait(m_job_queue_t *queue,
                                                      m_job_handler_t handler,
                                                      void *data)
{
    return m_job_queue_submit_nowait_with_handle(queue, handler, data, NULL);
}

/**
 * @brief   Submit a job handler with a deadline for queue capacity.
 */
m_job_error_t m_job_queue_submit_until_with_handle(m_job_queue_t *queue,
                                                   m_job_handler_t handler,
                                                   void *data,
                                                   const m_timer_deadline_t *deadline,
                                                   m_job_handle_t **out_handle);

static inline m_job_error_t m_job_queue_submit_until(m_job_queue_t *queue,
                                                     m_job_handler_t handler,
                                                     void *data,
                                                     const m_timer_deadline_t *deadline)
{
    return m_job_queue_submit_until_with_handle(queue,
                                                handler,
                                                data,
                                                deadline,
                                                NULL);
}

/**
 * @brief   Retrieve queue metadata.
 */
void m_job_queue_get_info(const m_job_queue_t *queue,
                          m_job_queue_info_t *info);

/**
 * @brief   Copy queue statistics.
 */
void m_job_queue_get_stats(const m_job_queue_t *queue, m_job_stats_t *stats);

#ifdef CONFIG_MAGNOLIA_JOB_SELFTESTS
/**
 * @brief   Retrieve a worker task identifier for introspection.
 */
m_sched_task_id_t m_job_queue_get_worker_task_id(const m_job_queue_t *queue,
                                                 size_t index);
#endif

/**
 * @brief   Internal helper used by workers to grab the next job.
 */
m_job_error_t _m_job_queue_take(m_job_queue_t *queue,
                                 m_job_handle_t **out,
                                 m_job_worker_t *worker);

#ifdef __cplusplus
}
#endif

#endif /* MAGNOLIA_JOB_M_JOB_QUEUE_H */
