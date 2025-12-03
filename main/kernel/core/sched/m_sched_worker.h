/**
 * @file kernel/core/sched/m_sched_worker.h
 * @brief Optional worker lifecycle helpers for Magnolia SAL.
 * @details Exposes instrumentation hooks so higher layers can track worker
 *          task lifecycles and future policy injections.
 */

#ifndef MAGNOLIA_SCHED_M_SCHED_WORKER_H
#define MAGNOLIA_SCHED_M_SCHED_WORKER_H

#include "kernel/core/sched/m_sched_core.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*m_sched_worker_lifecycle_hook_fn)(
        m_sched_task_id_t task_id,
        m_sched_task_metadata_t *metadata,
        void *user_data);

/**
 * @brief Hooks that will be invoked when a worker task transitions state.
 */
typedef struct {
    m_sched_worker_lifecycle_hook_fn on_worker_start;
    m_sched_worker_lifecycle_hook_fn on_worker_stop;
    void *user_data;
} m_sched_worker_hooks_t;

/**
 * @brief Register worker hooks for instrumentation and policy enforcement.
 *
 * @param hooks Hook set to install, or NULL to clear.
 */
void m_sched_worker_hooks_register(const m_sched_worker_hooks_t *hooks);

/**
 * @brief Internal notifier invoked when a registered worker begins running.
 *
 * @param meta Metadata for the task that just started.
 */
void _m_sched_worker_notify_start(m_sched_task_metadata_t *meta);

/**
 * @brief Internal notifier invoked when a registered worker stops running.
 *
 * @param meta Metadata for the task that just stopped.
 */
void _m_sched_worker_notify_stop(m_sched_task_metadata_t *meta);

#ifdef __cplusplus
}
#endif

#endif /* MAGNOLIA_SCHED_M_SCHED_WORKER_H */
