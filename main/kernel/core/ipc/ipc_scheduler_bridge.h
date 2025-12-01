#ifndef MAGNOLIA_IPC_SCHEDULER_BRIDGE_H
#define MAGNOLIA_IPC_SCHEDULER_BRIDGE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifndef portYIELD_CORE
#define portYIELD_CORE(x) portYIELD()
#endif

#include "kernel/core/sched/m_sched.h"
#include "kernel/core/timer/m_timer.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Result returned to IPC primitives when a wait completes.
 */
typedef enum {
    IPC_WAIT_RESULT_OK = 0,
    IPC_WAIT_RESULT_TIMEOUT,
    IPC_WAIT_RESULT_OBJECT_DESTROYED,
    IPC_WAIT_RESULT_SHUTDOWN,
} ipc_wait_result_t;

typedef struct ipc_waiter {
    struct ipc_waiter *prev;
    struct ipc_waiter *next;
    m_sched_wait_context_t ctx;
    bool enqueued;
} ipc_waiter_t;

typedef struct {
    ipc_waiter_t *head;
    ipc_waiter_t *tail;
    size_t count;
} ipc_wait_queue_t;

void ipc_wait_queue_init(ipc_wait_queue_t *queue);
void ipc_waiter_prepare(ipc_waiter_t *waiter);
void ipc_waiter_enqueue(ipc_wait_queue_t *queue, ipc_waiter_t *waiter);
bool ipc_waiter_remove(ipc_wait_queue_t *queue, ipc_waiter_t *waiter);
ipc_wait_result_t ipc_waiter_block(ipc_waiter_t *waiter,
                                   const m_timer_deadline_t *deadline);
ipc_wait_result_t ipc_waiter_timed_block(ipc_waiter_t *waiter,
                                          uint64_t timeout_us);
bool ipc_wake_one(ipc_wait_queue_t *queue, ipc_wait_result_t result);
void ipc_wake_all(ipc_wait_queue_t *queue, ipc_wait_result_t result);

#ifdef __cplusplus
}
#endif

#endif /* MAGNOLIA_IPC_SCHEDULER_BRIDGE_H */
