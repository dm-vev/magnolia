/**
 * @file kernel/core/sched/m_sched_worker.c
 * @brief Worker instrumentation helpers.
 * @details Tracks worker hook registrations and exposes lifecycle notifications
 *          that other core components can call without duplicating logic.
 */

#include <string.h>

#include "kernel/core/sched/m_sched_worker.h"
#include "kernel/core/sched/m_sched_core_internal.h"

static m_sched_worker_hooks_t g_worker_hooks;

/**
 * @brief Register worker lifecycle instrumentation hooks.
 */
void m_sched_worker_hooks_register(const m_sched_worker_hooks_t *hooks)
{
    _m_sched_registry_lock();
    if (hooks == NULL) {
        memset(&g_worker_hooks, 0, sizeof(g_worker_hooks));
        _m_sched_registry_unlock();
        return;
    }

    g_worker_hooks = *hooks;
    _m_sched_registry_unlock();
}

/**
 * @brief Notify registered hooks that a worker task has started.
 */
void _m_sched_worker_notify_start(m_sched_task_metadata_t *meta)
{
    if (meta == NULL || !(meta->creation_flags & M_SCHED_TASK_FLAG_WORKER)) {
        return;
    }

    m_sched_worker_hooks_t hooks;
    _m_sched_registry_lock();
    hooks = g_worker_hooks;
    _m_sched_registry_unlock();

    if (hooks.on_worker_start) {
        hooks.on_worker_start(meta->id, meta, hooks.user_data);
    }
}

/**
 * @brief Notify registered hooks that a worker task has stopped.
 */
void _m_sched_worker_notify_stop(m_sched_task_metadata_t *meta)
{
    if (meta == NULL || !(meta->creation_flags & M_SCHED_TASK_FLAG_WORKER)) {
        return;
    }

    m_sched_worker_hooks_t hooks;
    _m_sched_registry_lock();
    hooks = g_worker_hooks;
    _m_sched_registry_unlock();

    if (hooks.on_worker_stop) {
        hooks.on_worker_stop(meta->id, meta, hooks.user_data);
    }
}
