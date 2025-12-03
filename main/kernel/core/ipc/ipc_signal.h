/**
 * @file        ipc_signal.h
 * @brief       Public interface for Magnolia IPC signal primitives.
 * @details     Declares creation, destruction, and wait semantics for counting and one-shot signals.
 */

#ifndef MAGNOLIA_IPC_SIGNAL_H
#define MAGNOLIA_IPC_SIGNAL_H

#include <stdbool.h>
#include <stdint.h>

#include "kernel/core/ipc/ipc_core.h"
#include "kernel/core/ipc/ipc_waitset.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Signal operation mode selector.
 * @details Counting mode tracks arrival count while one-shot mode toggles readiness each set.
 */
typedef enum {
    IPC_SIGNAL_MODE_ONE_SHOT = 0,
    IPC_SIGNAL_MODE_COUNTING,
} ipc_signal_mode_t;

/**
 * @brief   Initialize every signal slot before IPC usage.
 * @details Clears counters, resets wait queues, and reinitializes locks so handles start in the not ready state.
 *
 * @return  void
 */
void ipc_signal_module_init(void);

/**
 * @brief   Allocate a new signal object and return its handle.
 * @details Initializes the requested mode, arms the wait queue, and leaves the signal in the not ready state until set.
 *
 * @param   mode          Signal operation mode selector.
 * @param   out_handle    Receives a valid handle when IPC_OK is returned.
 *
 * @return  IPC_OK                    Signal slot allocated and counters initialized.
 * @return  IPC_ERR_INVALID_ARGUMENT  Null handle pointer or registry not initialized.
 * @return  IPC_ERR_NO_SPACE          No free slots remain.
 */
ipc_error_t ipc_signal_create(ipc_signal_mode_t mode,
                              ipc_handle_t *out_handle);

/**
 * @brief   Destroy a signal and wake waiters with an object destroyed status.
 * @details Marks the object destroyed, clears pending state, releases the registry slot, and notifies subscribed waitsets.
 *
 * @param   handle        Handle referencing the signal to destroy.
 *
 * @return  IPC_OK                    Signal destroyed successfully.
 * @return  IPC_ERR_INVALID_HANDLE    Handle is invalid, not a signal, or already released.
 * @return  IPC_ERR_OBJECT_DESTROYED  Signal already destroyed.
 */
ipc_error_t ipc_signal_destroy(ipc_handle_t handle);

/**
 * @brief   Set a signal, waking one waiting task.
 * @details In counting mode increments the counter; in one-shot mode toggles pending readiness and wakes a waiter if present.
 *
 * @param   handle        Signal handle to set.
 *
 * @return  IPC_OK                    Signal set and readiness updated.
 * @return  IPC_ERR_INVALID_HANDLE    Handle is invalid or not a signal object.
 * @return  IPC_ERR_OBJECT_DESTROYED  Signal was destroyed during the operation.
 */
ipc_error_t ipc_signal_set(ipc_handle_t handle);

/**
 * @brief   Clear a signal to the not ready state and reset counters.
 * @details Clears pending flags and counters so the signal reports not ready until a new set occurs.
 *
 * @param   handle        Signal handle to clear.
 *
 * @return  IPC_OK                    Signal cleared successfully.
 * @return  IPC_ERR_INVALID_HANDLE    Handle is invalid or not a signal object.
 * @return  IPC_ERR_OBJECT_DESTROYED  Signal already marked destroyed.
 */
ipc_error_t ipc_signal_clear(ipc_handle_t handle);

/**
 * @brief   Attempt to consume a ready signal without blocking.
 * @details Consumes the signal in counting mode or clears the pending flag in one-shot mode when ready.
 *
 * @param   handle        Signal handle to consume.
 *
 * @return  IPC_OK                    Signal was ready and consumed.
 * @return  IPC_ERR_INVALID_HANDLE    Handle is invalid or not a signal object.
 * @return  IPC_ERR_OBJECT_DESTROYED  Signal was destroyed while checking readiness.
 * @return  IPC_ERR_NOT_READY         Signal reported not ready.
 */
ipc_error_t ipc_signal_try_wait(ipc_handle_t handle);

/**
 * @brief   Wait indefinitely until the signal becomes ready.
 * @details Blocks the calling task, tracks waits, and uses the IPC scheduler bridge wait reason for IPC primitives.
 *
 * @param   handle        Signal handle to wait on.
 *
 * @return  IPC_OK                    Signal became ready and was consumed.
 * @return  IPC_ERR_INVALID_HANDLE    Handle is invalid or not a signal object.
 * @return  IPC_ERR_OBJECT_DESTROYED  Signal destroyed while waiting.
 * @return  IPC_ERR_SHUTDOWN          Scheduler shutdown or unexpected wait result.
 */
ipc_error_t ipc_signal_wait(ipc_handle_t handle);

/**
 * @brief   Wait for the signal to become ready or for the deadline to expire.
 * @details Blocks the calling task until the signal is ready or the timeout is reached, tracking timeout statistics.
 *
 * @param   handle        Signal handle to wait on.
 * @param   timeout_us    Timeout in microseconds (M_TIMER_TIMEOUT_FOREVER for indefinite wait, 0 for immediate poll).
 *
 * @return  IPC_OK                    Signal became ready and was consumed.
 * @return  IPC_ERR_INVALID_HANDLE    Handle is invalid or not a signal object.
 * @return  IPC_ERR_OBJECT_DESTROYED  Signal destroyed while waiting.
 * @return  IPC_ERR_TIMEOUT           Timeout elapsed before the signal became ready.
 * @return  IPC_ERR_SHUTDOWN          Scheduler shutdown or unexpected wait result.
 */
ipc_error_t ipc_signal_timed_wait(ipc_handle_t handle, uint64_t timeout_us);

/**
 * @brief   Subscribe a listener to signal readiness notifications.
 * @details Adds the listener to the waitset list, captures callback/user data, and immediately dispatches current readiness state.
 *
 * @param   handle        Signal handle to observe.
 * @param   listener      Listener structure owned by the caller.
 * @param   callback      Callback invoked each time ready state changes.
 * @param   user_data     Opaque pointer provided to the callback.
 *
 * @return  IPC_OK                    Subscription succeeded and callback already invoked with the current ready state.
 * @return  IPC_ERR_INVALID_ARGUMENT  One of the listener or callback parameters is NULL.
 * @return  IPC_ERR_INVALID_HANDLE    Handle is invalid or not a signal object.
 */
ipc_error_t ipc_signal_waitset_subscribe(ipc_handle_t handle,
                                         ipc_waitset_listener_t *listener,
                                         ipc_waitset_ready_cb_t callback,
                                         void *user_data);

/**
 * @brief   Remove a listener from a signal waitset subscription.
 * @details Searches for the provided listener and unlinks it while holding the signal lock.
 *
 * @param   handle        Signal handle whose subscription should be removed.
 * @param   listener      Listener previously registered with subscribe.
 *
 * @return  IPC_OK                    Listener removed successfully.
 * @return  IPC_ERR_INVALID_ARGUMENT  Listener parameter is NULL.
 * @return  IPC_ERR_INVALID_HANDLE    Handle is invalid or not a signal object.
 */
ipc_error_t ipc_signal_waitset_unsubscribe(ipc_handle_t handle,
                                           ipc_waitset_listener_t *listener);

#ifdef __cplusplus
}
#endif

#endif /* MAGNOLIA_IPC_SIGNAL_H */
