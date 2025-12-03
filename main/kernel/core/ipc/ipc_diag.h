/*
 * Magnolia OS — IPC Subsystem
 * Purpose:
 *     Diagnostics helpers for Magnolia IPC objects.
 *
 * © 2025 Magnolia Project
 */

#ifndef MAGNOLIA_IPC_DIAG_H
#define MAGNOLIA_IPC_DIAG_H

#include <stdbool.h>
#include <stddef.h>

#include "kernel/core/ipc/ipc_channel.h"
#include "kernel/core/ipc/ipc_core.h"
#include "kernel/core/ipc/ipc_event_flags.h"
#include "kernel/core/ipc/ipc_shm.h"
#include "kernel/core/ipc/ipc_signal.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    ipc_object_type_t type;
    bool destroyed;
    size_t waiting_tasks;
} ipc_object_info_t;

typedef struct {
    ipc_signal_mode_t mode;
    bool ready;
    uint32_t count;
    size_t waiting_tasks;
    bool destroyed;
    uint32_t sets;
    uint32_t waits;
    uint32_t timeouts;
} ipc_signal_info_t;

typedef struct {
    uint32_t mask;
    ipc_event_flags_mode_t mode;
    ipc_event_flags_mask_mode_t mask_mode;
    size_t waiting_tasks;
    bool destroyed;
    bool ready;
    bool ready_for_mask;
    bool metadata_consistent;
    uint32_t sets;
    uint32_t waits;
    uint32_t timeouts;
} ipc_event_flags_info_t;

typedef struct {
    size_t capacity;
    size_t depth;
    size_t message_size;
    size_t waiting_senders;
    size_t waiting_receivers;
    bool destroyed;
    bool ready;
} ipc_channel_info_t;

/**
 * @brief Query the generic state of any IPC object.
 *
 * @param handle IPC handle.
 * @param info Receives common state (type, waiters, destroyed flag).
 * @return IPC_OK on success.
 * @return IPC_ERR_INVALID_ARGUMENT if @p info is NULL.
 * @return IPC_ERR_INVALID_HANDLE when the handle is unknown or mismatched.
 */
ipc_error_t ipc_diag_object_info(ipc_handle_t handle,
                                 ipc_object_info_t *info);

/**
 * @brief Query signal-specific diagnostics.
 *
 * @param handle Signal handle.
 * @param info Receives signal counters.
 * @return IPC_OK on success.
 * @return IPC_ERR_INVALID_ARGUMENT if @p info is NULL.
 * @return IPC_ERR_INVALID_HANDLE when the handle is invalid.
 */
ipc_error_t ipc_diag_signal_info(ipc_handle_t handle,
                                 ipc_signal_info_t *info);

/**
 * @brief Query channel-specific diagnostics.
 *
 * @param handle Channel handle.
 * @param info Receives capacity, depth, waiter counts, and readiness.
 * @return IPC_OK on success.
 * @return IPC_ERR_INVALID_ARGUMENT if @p info is NULL.
 * @return IPC_ERR_INVALID_HANDLE when the handle is invalid.
 */
ipc_error_t ipc_diag_channel_info(ipc_handle_t handle,
                                  ipc_channel_info_t *info);

ipc_error_t ipc_diag_event_flags_info(ipc_handle_t handle,
                                      uint32_t mask,
                                      ipc_event_flags_info_t *info);

ipc_error_t ipc_diag_shm_info(ipc_handle_t handle, ipc_shm_info_t *info);

#ifdef __cplusplus
}
#endif

#endif /* MAGNOLIA_IPC_DIAG_H */
