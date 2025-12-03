/*
 * Magnolia OS — IPC Subsystem
 * Purpose:
 *     Helper definitions for IPC waitset support (shared by primitives).
 *
 * © 2025 Magnolia Project
 */

#ifndef MAGNOLIA_IPC_WAITSET_H
#define MAGNOLIA_IPC_WAITSET_H

#include <stddef.h>

#include "kernel/core/ipc/ipc_core.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Waitset readiness callback.
 */
typedef void (*ipc_waitset_ready_cb_t)(ipc_handle_t handle,
                                       bool ready,
                                       void *user_data);

/**
 * @brief Internal waitset listener that waits can register.
 */
typedef struct ipc_waitset_listener {
    struct ipc_waitset_listener *next;
    ipc_waitset_ready_cb_t callback;
    void *user_data;
} ipc_waitset_listener_t;

#ifdef __cplusplus
}
#endif

#endif /* MAGNOLIA_IPC_WAITSET_H */
