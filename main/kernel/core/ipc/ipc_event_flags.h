/**
 * @file        main/kernel/core/ipc/ipc_event_flags.h
 * @brief       Public interfaces for the IPC event flags primitive.
 * @details     Defines creation, destruction, waiting, and waitset integration
 *              for event flag objects used by Magnolia tasks.
 */

#ifndef MAGNOLIA_IPC_EVENT_FLAGS_H
#define MAGNOLIA_IPC_EVENT_FLAGS_H

#include <stdbool.h>
#include <stdint.h>

#include "kernel/core/ipc/ipc_core.h"
#include "kernel/core/ipc/ipc_waitset.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Controls whether waits clear satisfied bits automatically.
 */
typedef enum {
    IPC_EVENT_FLAGS_MODE_AUTO_CLEAR = 0,
    IPC_EVENT_FLAGS_MODE_MANUAL_CLEAR,
} ipc_event_flags_mode_t;

/**
 * @brief Selects the mask-matching semantics available to wait-mask routines.
 */
typedef enum {
    IPC_EVENT_FLAGS_MASK_MODE_EXACT = 0,
    IPC_EVENT_FLAGS_MASK_MODE_SUPERSET,
} ipc_event_flags_mask_mode_t;

/**
 * @brief Enumerates the evaluation strategies used by wait and try-wait operations.
 */
typedef enum {
    IPC_EVENT_FLAGS_WAIT_ANY = 0,
    IPC_EVENT_FLAGS_WAIT_ALL,
    IPC_EVENT_FLAGS_WAIT_MASK,
} ipc_event_flags_wait_type_t;

/**
 * @brief Prepare the event flags subsystem prior to use.
 * @details Zeroes pooled registry state, initializes locks, and leaves every slot in a not ready state.
 */
void ipc_event_flags_module_init(void);

/**
 * @brief Allocate a fresh event flags record with the chosen semantics.
 * @details Reserves a handle, sets the readiness flag to not ready, and initializes queues and listeners.
 *
 * @param mode Auto-clear or manual-clear semantics.
 * @param mask_mode Mask matching behavior for wait-mask operations.
 * @param out_handle Receives the handle when creation succeeds.
 *
 * @return IPC_OK when creation succeeds and the handle is ready for use.
 * @return IPC_ERR_INVALID_ARGUMENT when any argument is invalid.
 * @return IPC_ERR_NO_SPACE when the registry cannot allocate a new slot.
 */
ipc_error_t ipc_event_flags_create(ipc_event_flags_mode_t mode,
                                   ipc_event_flags_mask_mode_t mask_mode,
                                   ipc_handle_t *out_handle);

/**
 * @brief Transition the event flags object into destroyed state and release its handle.
 * @details Wakes all waiters and waitset listeners with destroyed notifications and resets readiness to not ready.
 *
 * @param handle Target handle.
 *
 * @return IPC_OK when destruction succeeds.
 * @return IPC_ERR_INVALID_HANDLE when the handle is malformed or unknown.
 * @return IPC_ERR_OBJECT_DESTROYED when the object was already destroyed.
 */
ipc_error_t ipc_event_flags_destroy(ipc_handle_t handle);

/**
 * @brief Set the supplied bits and update readiness.
 * @details Combines the bits, updates ready/not ready state, and services waiters until no new readiness transitions occur.
 *
 * @param handle Target handle.
 * @param bits Bits to set (OR).
 *
 * @return IPC_OK when the mask was updated successfully.
 * @return IPC_ERR_INVALID_HANDLE when the handle is malformed.
 * @return IPC_ERR_OBJECT_DESTROYED when the object became destroyed while the lock was held.
 */
ipc_error_t ipc_event_flags_set(ipc_handle_t handle, uint32_t bits);

/**
 * @brief Clear the supplied bits from the active mask.
 * @details Adjusts readiness to not ready if the cleared bits remove satisfied predicates.
 *
 * @param handle Target handle.
 * @param bits Bits to clear (AND NOT).
 *
 * @return IPC_OK when the mask was cleared successfully.
 * @return IPC_ERR_INVALID_HANDLE when the handle is malformed.
 * @return IPC_ERR_OBJECT_DESTROYED when the object became destroyed while the lock was held.
 */
ipc_error_t ipc_event_flags_clear(ipc_handle_t handle, uint32_t bits);

/**
 * @brief Read the current mask without affecting readiness.
 *
 * @param handle Target handle.
 * @param out_mask Receives the active bits.
 *
 * @return IPC_OK when the mask was read.
 * @return IPC_ERR_INVALID_ARGUMENT when out_mask is NULL.
 * @return IPC_ERR_INVALID_HANDLE when the handle is malformed.
 * @return IPC_ERR_OBJECT_DESTROYED when the object has been destroyed.
 */
ipc_error_t ipc_event_flags_read(ipc_handle_t handle, uint32_t *out_mask);

/**
 * @brief Try to satisfy the predicate without blocking.
 * @details Matched bits may be auto-cleared when the condition succeeds.
 *
 * @param handle Target handle.
 * @param wait_type Evaluation kind (any/all/mask).
 * @param mask Required bits for the wait.
 *
 * @return IPC_OK when the predicate was satisfied immediately.
 * @return IPC_ERR_NOT_READY when the predicate is still unmet.
 * @return IPC_ERR_INVALID_ARGUMENT when mask is zero or wait type invalid.
 * @return IPC_ERR_INVALID_HANDLE when the handle is malformed.
 * @return IPC_ERR_OBJECT_DESTROYED when the object was destroyed.
 */
ipc_error_t ipc_event_flags_try_wait(ipc_handle_t handle,
                                     ipc_event_flags_wait_type_t wait_type,
                                     uint32_t mask);

/**
 * @brief Block until the requested condition reaches ready.
 * @details Magnolia scheduler semantics apply; the caller sleeps until readiness, destruction, or shutdown occurs.
 *
 * @param handle Target handle.
 * @param wait_type Evaluation kind (any/all/mask).
 * @param mask Required bits for the wait.
 *
 * @return IPC_OK when readiness is reached.
 * @return IPC_ERR_INVALID_ARGUMENT when mask is zero or wait type invalid.
 * @return IPC_ERR_INVALID_HANDLE when the handle is malformed.
 * @return IPC_ERR_OBJECT_DESTROYED when the object is destroyed while waiting.
 * @return IPC_ERR_SHUTDOWN for unexpected scheduler shutdown semantics.
 */
ipc_error_t ipc_event_flags_wait(ipc_handle_t handle,
                                 ipc_event_flags_wait_type_t wait_type,
                                 uint32_t mask);

/**
 * @brief Block until readiness or the specified deadline.
 * @details Uses Magnolia timer semantics and wakes the caller with timeout, ready, or destroyed states.
 *
 * @param handle Target handle.
 * @param wait_type Evaluation kind (any/all/mask).
 * @param mask Required bits for the wait.
 * @param timeout_us Relative deadline in microseconds.
 *
 * @return IPC_OK when readiness is reached before the timeout.
 * @return IPC_ERR_TIMEOUT when the deadline expires first.
 * @return IPC_ERR_INVALID_ARGUMENT when mask is zero or wait type invalid.
 * @return IPC_ERR_INVALID_HANDLE when the handle is malformed.
 * @return IPC_ERR_OBJECT_DESTROYED when the object is destroyed while waiting.
 * @return IPC_ERR_SHUTDOWN for unexpected scheduler shutdown semantics.
 */
ipc_error_t ipc_event_flags_timed_wait(ipc_handle_t handle,
                                       ipc_event_flags_wait_type_t wait_type,
                                       uint32_t mask,
                                       uint64_t timeout_us);

/**
 * @brief Subscribe a waitset listener to readiness transitions.
 * @details The callback is invoked immediately with the current ready state and thereafter when readiness changes.
 *
 * @param handle Target handle.
 * @param listener Listener object supplied by the caller.
 * @param callback Callback invoked with ready/not ready transitions.
 * @param user_data Arbitrary user context passed to the callback.
 *
 * @return IPC_OK when the listener was registered.
 * @return IPC_ERR_INVALID_ARGUMENT when listener or callback is NULL.
 * @return IPC_ERR_INVALID_HANDLE when the handle is malformed.
 */
ipc_error_t ipc_event_flags_waitset_subscribe(ipc_handle_t handle,
                                              ipc_waitset_listener_t *listener,
                                              ipc_waitset_ready_cb_t callback,
                                              void *user_data);

/**
 * @brief Unsubscribe a waitset listener from readiness notifications.
 * @details Stops future callbacks for the provided listener while leaving other subscribers untouched.
 *
 * @param handle Target handle.
 * @param listener Listener object previously registered.
 *
 * @return IPC_OK when the listener was removed.
 * @return IPC_ERR_INVALID_ARGUMENT when listener is NULL.
 * @return IPC_ERR_INVALID_HANDLE when the handle is malformed.
 */
ipc_error_t ipc_event_flags_waitset_unsubscribe(ipc_handle_t handle,
                                                ipc_waitset_listener_t *listener);

#ifdef __cplusplus
}
#endif

#endif /* MAGNOLIA_IPC_EVENT_FLAGS_H */
