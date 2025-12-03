/**
 * @file        main/kernel/core/ipc/ipc_event_flags_private.h
 * @brief       Internal helpers for the event flags primitive.
 * @details     Exposes private structures and helpers used by the public API implementation.
 */

#ifndef MAGNOLIA_IPC_EVENT_FLAGS_PRIVATE_H
#define MAGNOLIA_IPC_EVENT_FLAGS_PRIVATE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "kernel/core/ipc/ipc_core.h"
#include "kernel/core/ipc/ipc_event_flags.h"
#include "kernel/core/ipc/ipc_scheduler_bridge.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Tracks a waiting task and its predicate for the event flags queue.
 */
typedef struct ipc_event_flags_waiter {
    ipc_waiter_t wait;
    ipc_event_flags_wait_type_t wait_type;
    uint32_t mask;
} ipc_event_flags_waiter_t;

/**
 * @brief Represents the internal state tracked for each event flags object.
 */
typedef struct ipc_event_flags {
    ipc_object_header_t header;
    uint32_t mask;
    ipc_event_flags_mode_t mode;
    ipc_event_flags_mask_mode_t mask_mode;
    bool ready_state;
    ipc_wait_queue_t waiters;
    ipc_waitset_listener_t *listeners;
    struct {
        uint32_t sets;
        uint32_t clears;
        uint32_t waits;
        uint32_t timeouts;
    } stats;
} ipc_event_flags_t;

/**
 * @brief Lookup an event flags object by handle after validating ownership.
 *
 * @param handle Event flags handle.
 * @return Pointer to the object when the handle is valid; NULL otherwise.
 */
ipc_event_flags_t *ipc_event_flags_lookup(ipc_handle_t handle);

/**
 * @brief See `ipc_event_flags_module_init` for the external description.
 */
void ipc_event_flags_module_init(void);

#ifdef __cplusplus
}
#endif

#endif /* MAGNOLIA_IPC_EVENT_FLAGS_PRIVATE_H */
