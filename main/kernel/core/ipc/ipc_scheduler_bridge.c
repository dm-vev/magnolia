/*
 * Magnolia OS — IPC Subsystem
 * Purpose:
 *     Scheduler bridge implementation for IPC wait queues.
 *
 * © 2025 Magnolia Project
 */

#include "kernel/core/ipc/ipc_scheduler_bridge.h"

#include <string.h>

#include "freertos/task.h"

static m_sched_wait_result_t ipc_bridge_map_to_sched(ipc_wait_result_t result)
{
    switch (result) {
    case IPC_WAIT_RESULT_OK:
        return M_SCHED_WAIT_RESULT_OK;
    case IPC_WAIT_RESULT_TIMEOUT:
        return M_SCHED_WAIT_RESULT_TIMEOUT;
    case IPC_WAIT_RESULT_OBJECT_DESTROYED:
        return M_SCHED_WAIT_RESULT_OBJECT_DESTROYED;
    default:
        return M_SCHED_WAIT_RESULT_SHUTDOWN;
    }
}

static ipc_wait_result_t ipc_bridge_map_from_sched(m_sched_wait_result_t result)
{
    switch (result) {
    case M_SCHED_WAIT_RESULT_OK:
        return IPC_WAIT_RESULT_OK;
    case M_SCHED_WAIT_RESULT_TIMEOUT:
        return IPC_WAIT_RESULT_TIMEOUT;
    case M_SCHED_WAIT_RESULT_OBJECT_DESTROYED:
        return IPC_WAIT_RESULT_OBJECT_DESTROYED;
    default:
        return IPC_WAIT_RESULT_SHUTDOWN;
    }
}

void ipc_wait_queue_init(ipc_wait_queue_t *queue)
{
    if (queue == NULL) {
        return;
    }

    queue->head = NULL;
    queue->tail = NULL;
    queue->count = 0;
}

void ipc_waiter_prepare(ipc_waiter_t *waiter,
                        m_sched_wait_reason_t reason)
{
    if (waiter == NULL) {
        return;
    }

    memset(waiter, 0, sizeof(*waiter));
    waiter->enqueued = false;
    m_sched_wait_context_prepare_with_reason(&waiter->ctx, reason);
}

void ipc_waiter_enqueue(ipc_wait_queue_t *queue, ipc_waiter_t *waiter)
{
    if (queue == NULL || waiter == NULL) {
        return;
    }

    waiter->prev = queue->tail;
    waiter->next = NULL;
    waiter->enqueued = true;

    if (queue->tail != NULL) {
        queue->tail->next = waiter;
    } else {
        queue->head = waiter;
    }

    queue->tail = waiter;
    queue->count++;
}

bool ipc_waiter_remove(ipc_wait_queue_t *queue, ipc_waiter_t *waiter)
{
    if (queue == NULL || waiter == NULL || !waiter->enqueued) {
        return false;
    }

    if (waiter->prev != NULL) {
        waiter->prev->next = waiter->next;
    } else {
        queue->head = waiter->next;
    }

    if (waiter->next != NULL) {
        waiter->next->prev = waiter->prev;
    } else {
        queue->tail = waiter->prev;
    }

    waiter->prev = NULL;
    waiter->next = NULL;
    waiter->enqueued = false;
    queue->count--;
    return true;
}

static ipc_waiter_t *ipc_wait_queue_pick(ipc_wait_queue_t *queue)
{
    if (queue == NULL || queue->head == NULL) {
        return NULL;
    }

    ipc_waiter_t *best = queue->head;
    UBaseType_t best_prio = uxTaskPriorityGet(best->ctx.task);
    for (ipc_waiter_t *iter = best->next; iter != NULL; iter = iter->next) {
        UBaseType_t iter_prio = uxTaskPriorityGet(iter->ctx.task);
        if (iter_prio > best_prio) {
            best = iter;
            best_prio = iter_prio;
        }
    }

    return best;
}

ipc_wait_result_t ipc_waiter_block(ipc_waiter_t *waiter,
                                   const m_timer_deadline_t *deadline)
{
    if (waiter == NULL) {
        return IPC_WAIT_RESULT_SHUTDOWN;
    }

    m_sched_wait_result_t sched_result = m_sched_wait_block(&waiter->ctx,
                                                            deadline);
    return ipc_bridge_map_from_sched(sched_result);
}

ipc_wait_result_t ipc_waiter_timed_block(ipc_waiter_t *waiter,
                                         uint64_t timeout_us)
{
    if (waiter == NULL) {
        return IPC_WAIT_RESULT_SHUTDOWN;
    }

    m_timer_deadline_t deadline = m_timer_deadline_from_relative(timeout_us);
    return ipc_waiter_block(waiter, &deadline);
}

bool ipc_wake_one(ipc_wait_queue_t *queue, ipc_wait_result_t result)
{
    if (queue == NULL) {
        return false;
    }

    ipc_waiter_t *candidate = ipc_wait_queue_pick(queue);
    if (candidate == NULL) {
        return false;
    }

    ipc_waiter_remove(queue, candidate);
    m_sched_wait_wake(&candidate->ctx, ipc_bridge_map_to_sched(result));
    return true;
}

void ipc_wake_all(ipc_wait_queue_t *queue, ipc_wait_result_t result)
{
    if (queue == NULL) {
        return;
    }

    ipc_waiter_t *current = queue->head;
    while (current != NULL) {
        ipc_waiter_t *next = current->next;
        ipc_waiter_remove(queue, current);
        m_sched_wait_wake(&current->ctx, ipc_bridge_map_to_sched(result));
        current = next;
    }
}
