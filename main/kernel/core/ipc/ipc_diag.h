#ifndef MAGNOLIA_IPC_DIAG_H
#define MAGNOLIA_IPC_DIAG_H

#include <stdbool.h>
#include <stddef.h>

#include "kernel/core/ipc/ipc_core.h"
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

ipc_error_t ipc_diag_object_info(ipc_handle_t handle,
                                 ipc_object_info_t *info);
ipc_error_t ipc_diag_signal_info(ipc_handle_t handle,
                                  ipc_signal_info_t *info);

#ifdef __cplusplus
}
#endif

#endif /* MAGNOLIA_IPC_DIAG_H */
