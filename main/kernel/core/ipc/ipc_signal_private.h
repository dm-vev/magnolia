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

typedef struct ipc_signal {
    ipc_object_header_t header;
    ipc_signal_mode_t mode;
    bool pending;
    uint32_t counter;
    bool ready_state;
    ipc_wait_queue_t waiters;
    ipc_waitset_listener_t *listeners;
    struct {
        uint32_t sets;
        uint32_t waits;
        uint32_t timeouts;
    } stats;
} ipc_signal_t;

ipc_signal_t *ipc_signal_lookup(ipc_handle_t handle);
void ipc_signal_module_init(void);

#ifdef __cplusplus
}
#endif

#endif /* MAGNOLIA_IPC_SIGNAL_PRIVATE_H */
