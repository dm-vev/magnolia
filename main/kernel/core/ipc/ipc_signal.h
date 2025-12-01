#ifndef MAGNOLIA_IPC_SIGNAL_H
#define MAGNOLIA_IPC_SIGNAL_H

#include <stdbool.h>
#include <stdint.h>

#include "kernel/core/ipc/ipc_core.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Signal operation mode.
 */
typedef enum {
    IPC_SIGNAL_MODE_ONE_SHOT = 0,
    IPC_SIGNAL_MODE_COUNTING,
} ipc_signal_mode_t;

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

void ipc_signal_module_init(void);
ipc_error_t ipc_signal_create(ipc_signal_mode_t mode,
                              ipc_handle_t *out_handle);
ipc_error_t ipc_signal_destroy(ipc_handle_t handle);
ipc_error_t ipc_signal_set(ipc_handle_t handle);
ipc_error_t ipc_signal_clear(ipc_handle_t handle);
ipc_error_t ipc_signal_wait(ipc_handle_t handle);
ipc_error_t ipc_signal_try_wait(ipc_handle_t handle);
ipc_error_t ipc_signal_timed_wait(ipc_handle_t handle, uint64_t timeout_us);
ipc_error_t ipc_signal_waitset_subscribe(ipc_handle_t handle,
                                         ipc_waitset_listener_t *listener,
                                         ipc_waitset_ready_cb_t callback,
                                         void *user_data);
ipc_error_t ipc_signal_waitset_unsubscribe(ipc_handle_t handle,
                                           ipc_waitset_listener_t *listener);

#ifdef __cplusplus
}
#endif

#endif /* MAGNOLIA_IPC_SIGNAL_H */
