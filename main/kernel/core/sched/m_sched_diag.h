/**
 * @file kernel/core/sched/m_sched_diag.h
 * @brief Scheduler diagnostics and introspection helpers.
 * @details Provides snapshots and metadata queries without performing any
 *          scheduling decisions.
 */

#ifndef MAGNOLIA_SCHED_M_SCHED_DIAG_H
#define MAGNOLIA_SCHED_M_SCHED_DIAG_H

#include "kernel/core/sched/m_sched_core.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Simplified metadata entry returned by diagnostics.
 */
typedef struct {
    m_sched_task_id_t id;
    char name[configMAX_TASK_NAME_LEN];
    m_sched_task_state_t state;
    m_sched_wait_reason_t wait_reason;
    char tag[M_SCHED_TASK_TAG_MAX_LEN];
} m_sched_task_diag_entry_t;

/**
 * @brief Copy up to @p capacity task metadata entries.
 *
 * @param buffer Output buffer to populate.
 * @param capacity Maximum number of entries to fill.
 * @return Number of entries written.
 */
size_t m_sched_task_snapshot(m_sched_task_diag_entry_t *buffer,
                             size_t capacity);

/**
 * @brief Retrieve metadata for a single task by id.
 *
 * @param id Task identifier to look up.
 * @param out Output buffer that receives a copy of the metadata.
 * @return true if the task was found.
 */
bool m_sched_task_metadata_get(m_sched_task_id_t id,
                               m_sched_task_metadata_t *out);

/**
 * @brief Verify that the provided task id is known to the scheduler.
 *
 * @param id Candidate task identifier.
 * @return true if the id exists.
 */
bool m_sched_task_id_is_valid(m_sched_task_id_t id);

#ifdef __cplusplus
}
#endif

#endif /* MAGNOLIA_SCHED_M_SCHED_DIAG_H */
