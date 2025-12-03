/**
 * @file kernel/core/sched/m_sched_diag.c
 * @brief Scheduler diagnostics implementation.
 * @details Provides snapshots and metadata helpers without invoking scheduler
 *          decisions or wake logic.
 */

#include <string.h>

#include "kernel/core/sched/m_sched_diag.h"
#include "kernel/core/sched/m_sched_core_internal.h"

/**
 * @brief Internal context used while building a snapshot.
 */
typedef struct {
    m_sched_task_diag_entry_t *buffer;
    size_t capacity;
    size_t count;
} m_sched_diag_snapshot_ctx_t;

/**
 * @brief Callback used by @c m_sched_task_snapshot to copy registry entries.
 */
static bool m_sched_diag_snapshot_cb(m_sched_task_metadata_t *meta,
                                     void *user_data)
{
    if (meta == NULL || user_data == NULL) {
        return true;
    }

    m_sched_diag_snapshot_ctx_t *ctx = user_data;
    if (ctx->count >= ctx->capacity) {
        return false;
    }

    m_sched_task_diag_entry_t *entry = &ctx->buffer[ctx->count];
    entry->id = meta->id;
    strncpy(entry->name, meta->name, configMAX_TASK_NAME_LEN);
    entry->name[configMAX_TASK_NAME_LEN - 1] = '\0';
    entry->state = meta->state;
    entry->wait_reason = meta->wait_reason;
    strncpy(entry->tag, meta->tag, M_SCHED_TASK_TAG_MAX_LEN);
    entry->tag[M_SCHED_TASK_TAG_MAX_LEN - 1] = '\0';
    ctx->count++;
    return ctx->count < ctx->capacity;
}

/**
 * @brief Provide a snapshot of the current scheduler registry.
 */
size_t m_sched_task_snapshot(m_sched_task_diag_entry_t *buffer,
                             size_t capacity)
{
    if (buffer == NULL || capacity == 0) {
        return 0;
    }

    m_sched_diag_snapshot_ctx_t ctx = {
        .buffer = buffer,
        .capacity = capacity,
        .count = 0,
    };

    _m_sched_registry_iterate(m_sched_diag_snapshot_cb, &ctx);
    return ctx.count;
}

/**
 * @brief Copy the metadata for the task with the provided id.
 */
bool m_sched_task_metadata_get(m_sched_task_id_t id,
                               m_sched_task_metadata_t *out)
{
    if (id == M_SCHED_TASK_ID_INVALID || out == NULL) {
        return false;
    }

    _m_sched_registry_lock();
    m_sched_task_metadata_t *meta = _m_sched_metadata_find_locked_by_id(id);
    if (meta == NULL) {
        _m_sched_registry_unlock();
        return false;
    }

    *out = *meta;
    out->next = NULL;
    _m_sched_registry_unlock();
    return true;
}

/**
 * @brief Check whether a task identifier exists in the registry.
 */
bool m_sched_task_id_is_valid(m_sched_task_id_t id)
{
    if (id == M_SCHED_TASK_ID_INVALID) {
        return false;
    }

    _m_sched_registry_lock();
    bool found = _m_sched_metadata_find_locked_by_id(id) != NULL;
    _m_sched_registry_unlock();
    return found;
}
