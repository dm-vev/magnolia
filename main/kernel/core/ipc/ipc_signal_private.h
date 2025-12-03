/**
 * @file        ipc_signal_private.h
 * @brief       Private declarations for the Magnolia IPC signal primitive.
 * @details     Defines the internal state descriptor and helpers used by signal operations.
 */

#ifndef MAGNOLIA_IPC_SIGNAL_PRIVATE_H
#define MAGNOLIA_IPC_SIGNAL_PRIVATE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "kernel/core/ipc/ipc_core.h"
#include "kernel/core/ipc/ipc_scheduler_bridge.h"
#include "kernel/core/ipc/ipc_signal.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief   Runtime state stored per signal.
 * @details Tracks pending state, readiness, waiters, and statistical counters for diagnostics.
 */
typedef struct ipc_signal {
    ipc_object_header_t header;
    ipc_signal_mode_t mode;
    bool pending;
    uint32_t counter;
    bool ready_state;
    ipc_wait_queue_t waiters;
    ipc_waitset_listener_t *listeners;
    size_t waitset_listeners;
    struct {
        uint32_t sets;
        uint32_t waits;
        uint32_t timeouts;
    } stats;
} ipc_signal_t;

/**
 * @brief   Resolve the internal descriptor from a handle if it refers to a signal.
 *
 * @param   handle        Handle to inspect.
 *
 * @return  Pointer to the matching signal, or NULL if the handle is invalid or points elsewhere.
 */
ipc_signal_t *ipc_signal_lookup(ipc_handle_t handle);

void ipc_signal_module_init(void);

#ifdef __cplusplus
}
#endif

#endif /* MAGNOLIA_IPC_SIGNAL_PRIVATE_H */
