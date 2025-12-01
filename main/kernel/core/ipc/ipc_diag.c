#include "kernel/core/ipc/ipc_diag.h"
#include "kernel/core/ipc/ipc_signal_private.h"

static bool ipc_signal_is_ready_state(const ipc_signal_t *signal)
{
    if (signal->mode == IPC_SIGNAL_MODE_COUNTING) {
        return (signal->counter > 0);
    }
    return signal->pending;
}

ipc_error_t ipc_diag_object_info(ipc_handle_t handle,
                                 ipc_object_info_t *info)
{
    if (info == NULL) {
        return IPC_ERR_INVALID_ARGUMENT;
    }

    ipc_signal_t *signal = ipc_signal_lookup(handle);
    if (signal == NULL) {
        return IPC_ERR_INVALID_HANDLE;
    }

    portENTER_CRITICAL(&signal->header.lock);
    info->type = signal->header.type;
    info->destroyed = signal->header.destroyed;
    info->waiting_tasks = signal->header.waiting_tasks;
    portEXIT_CRITICAL(&signal->header.lock);

    return IPC_OK;
}

ipc_error_t ipc_diag_signal_info(ipc_handle_t handle,
                                  ipc_signal_info_t *info)
{
    if (info == NULL) {
        return IPC_ERR_INVALID_ARGUMENT;
    }

    ipc_signal_t *signal = ipc_signal_lookup(handle);
    if (signal == NULL) {
        return IPC_ERR_INVALID_HANDLE;
    }

    portENTER_CRITICAL(&signal->header.lock);
    info->mode = signal->mode;
    info->ready = ipc_signal_is_ready_state(signal);
    info->count = signal->mode == IPC_SIGNAL_MODE_COUNTING
                  ? signal->counter
                  : (signal->pending ? 1U : 0U);
    info->waiting_tasks = signal->header.waiting_tasks;
    info->destroyed = signal->header.destroyed;
    info->sets = signal->stats.sets;
    info->waits = signal->stats.waits;
    info->timeouts = signal->stats.timeouts;
    portEXIT_CRITICAL(&signal->header.lock);

    return IPC_OK;
}
