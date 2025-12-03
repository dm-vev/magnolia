/**
 * @file kernel/core/sched/m_sched_wait.h
 * @brief Waiting primitives for Magnolia SAL.
 * @details Implements the Magnus wait context and exposes blocking helpers used
 *          by IPC and jobs while keeping scheduling logic isolated.
 */

#ifndef MAGNOLIA_SCHED_M_SCHED_WAIT_H
#define MAGNOLIA_SCHED_M_SCHED_WAIT_H

#include "kernel/core/sched/m_sched_core.h"
#include "kernel/core/timer/m_timer.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Context maintained while a Magnolia task is blocked.
 */
typedef struct {
    SemaphoreHandle_t semaphore;
    StaticSemaphore_t storage;
    TaskHandle_t task;
    m_sched_task_metadata_t *owner;
    m_sched_wait_reason_t reason;
    m_sched_wait_result_t result;
    bool armed;
    bool initialized;
} m_sched_wait_context_t;

/**
 * @brief Prepare a wait context for generic events.
 *
 * @param ctx Context to initialize.
 */
void m_sched_wait_context_prepare(m_sched_wait_context_t *ctx);

/**
 * @brief Prepare a wait context with an explicit wait reason.
 *
 * @param ctx Context to initialize.
 * @param reason Reason code that will be logged while the task is blocked.
 */
void m_sched_wait_context_prepare_with_reason(m_sched_wait_context_t *ctx,
                                               m_sched_wait_reason_t reason);

/**
 * @brief Block the current task until the context is woken or the deadline
 *        expires.
 *
 * @param ctx Prepared wait context.
 * @param deadline Deadline to observe, or NULL for an infinite wait.
 * @return Result of the wait operation.
 */
m_sched_wait_result_t m_sched_wait_block(
        m_sched_wait_context_t *ctx, const m_timer_deadline_t *deadline);

/**
 * @brief Wake a task that is blocked on the provided context.
 *
 * @param ctx Context that was armed for waiting.
 * @param result Outcome that will be reported to the waiter.
 */
void m_sched_wait_wake(m_sched_wait_context_t *ctx,
                       m_sched_wait_result_t result);

#ifdef __cplusplus
}
#endif

#endif /* MAGNOLIA_SCHED_M_SCHED_WAIT_H */
