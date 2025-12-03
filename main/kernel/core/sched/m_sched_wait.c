/**
 * @file kernel/core/sched/m_sched_wait.c
 * @brief Magnolia wait context implementation.
 * @details Handles blocking waits, timed waits, and integrations with the
 *          timer subsystem while keeping scheduler-wide logic isolated.
 */

#include "kernel/core/sched/m_sched_wait.h"
#include "kernel/core/sched/m_sched_core_internal.h"

#include "freertos/semphr.h"
#include "freertos/task.h"

/**
 * @brief Update a task's scheduler state under the registry lock.
 */
static void m_sched_wait_update_state(m_sched_task_metadata_t *meta,
                                      m_sched_task_state_t state)
{
    if (meta == NULL) {
        return;
    }

    _m_sched_registry_lock();
    meta->state = state;
    _m_sched_registry_unlock();
}

/**
 * @brief Update a task's wait reason under the registry lock.
 */
static void m_sched_wait_update_wait_reason(
        m_sched_task_metadata_t *meta, m_sched_wait_reason_t reason)
{
    if (meta == NULL) {
        return;
    }

    _m_sched_registry_lock();
    meta->wait_reason = reason;
    _m_sched_registry_unlock();
}

/**
 * @brief Prepare a generic wait context for the current task.
 */
void m_sched_wait_context_prepare(m_sched_wait_context_t *ctx)
{
    m_sched_wait_context_prepare_with_reason(ctx,
                                             M_SCHED_WAIT_REASON_EVENT);
}

/**
 * @brief Prepare a wait context and record the blocking reason.
 */
void m_sched_wait_context_prepare_with_reason(
        m_sched_wait_context_t *ctx, m_sched_wait_reason_t reason)
{
    if (ctx == NULL) {
        return;
    }

    if (!ctx->initialized) {
        ctx->semaphore = xSemaphoreCreateBinaryStatic(&ctx->storage);
        ctx->initialized = (ctx->semaphore != NULL);
    }

    ctx->task = xTaskGetCurrentTaskHandle();
    ctx->reason = reason;
    ctx->armed = true;
    ctx->result = M_SCHED_WAIT_RESULT_OK;
    ctx->owner = NULL;

    if (!ctx->initialized) {
        return;
    }

    _m_sched_registry_lock();
    ctx->owner =
            _m_sched_metadata_find_locked_by_handle(ctx->task);
    _m_sched_registry_unlock();
}

/**
 * @brief Block the current task on a wait context and optional deadline.
 */
m_sched_wait_result_t m_sched_wait_block(
        m_sched_wait_context_t *ctx, const m_timer_deadline_t *deadline)
{
    if (ctx == NULL || ctx->semaphore == NULL) {
        return M_SCHED_WAIT_RESULT_SHUTDOWN;
    }

    if (ctx->owner != NULL) {
        m_sched_wait_update_wait_reason(ctx->owner, ctx->reason);
        m_sched_wait_update_state(ctx->owner, M_SCHED_STATE_WAITING);
    }

    m_timer_deadline_t infinite = {.infinite = true, .target = 0};
    const m_timer_deadline_t *use_deadline =
            deadline ? deadline : &infinite;

    TickType_t ticks = m_timer_deadline_to_ticks(use_deadline);
    BaseType_t taken = xSemaphoreTake(ctx->semaphore, ticks);

    ctx->armed = false;
    if (ctx->owner != NULL) {
        m_sched_wait_update_wait_reason(ctx->owner,
                                        M_SCHED_WAIT_REASON_NONE);
        m_sched_wait_update_state(ctx->owner, M_SCHED_STATE_READY);
    }

    if (taken == pdTRUE) {
        return ctx->result;
    }

    ctx->result = (ctx->reason == M_SCHED_WAIT_REASON_DELAY)
                          ? M_SCHED_WAIT_RESULT_OK
                          : M_SCHED_WAIT_RESULT_TIMEOUT;
    return ctx->result;
}

/**
 * @brief Wake a task that is waiting on the provided context.
 */
void m_sched_wait_wake(m_sched_wait_context_t *ctx,
                       m_sched_wait_result_t result)
{
    if (ctx == NULL || ctx->semaphore == NULL) {
        return;
    }

    ctx->result = result;
    if (!ctx->armed) {
        return;
    }

    ctx->armed = false;
    xSemaphoreGive(ctx->semaphore);
}
