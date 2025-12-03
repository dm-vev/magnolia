/**
 * @file kernel/core/sched/m_sched_core_internal.h
 * @brief Internal helpers shared between scheduler modules.
 * @details Provides lock primitives and registry iteration helpers for wait,
 *          diagnostics, and worker plumbing without exposing them publicly.
 */

#ifndef MAGNOLIA_SCHED_M_SCHED_CORE_INTERNAL_H
#define MAGNOLIA_SCHED_M_SCHED_CORE_INTERNAL_H

#include "kernel/core/sched/m_sched_core.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Callback invoked for each task metadata entry under the registry lock.
 *
 * @param meta Task metadata currently being visited.
 * @param user_data User-supplied pointer passed through.
 * @return true to continue iteration, false to stop early.
 */
typedef bool (*_m_sched_registry_iter_cb)(m_sched_task_metadata_t *meta,
                                          void *user_data);

/**
 * @brief Acquire the scheduler registry mutex.
 */
void _m_sched_registry_lock(void);

/**
 * @brief Release the scheduler registry mutex.
 */
void _m_sched_registry_unlock(void);

/**
 * @brief Find metadata by identifier while the registry lock is held.
 */
m_sched_task_metadata_t *_m_sched_metadata_find_locked_by_id(
        m_sched_task_id_t id);

/**
 * @brief Find metadata by handle while the registry lock is held.
 */
m_sched_task_metadata_t *_m_sched_metadata_find_locked_by_handle(
        TaskHandle_t handle);

/**
 * @brief Iterate through the registry while holding the lock.
 *
 * @param callback Visitor invoked for each metadata entry.
 * @param user_data Data forwarded to every callback invocation.
 * @return true if the iteration ran to completion, false if the callback
 *         requested an early exit.
 */
bool _m_sched_registry_iterate(_m_sched_registry_iter_cb callback,
                                void *user_data);

#ifdef __cplusplus
}
#endif

#endif /* MAGNOLIA_SCHED_M_SCHED_CORE_INTERNAL_H */
