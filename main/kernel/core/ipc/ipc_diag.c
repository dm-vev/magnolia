/*
 * Magnolia OS — IPC Subsystem
 * Purpose:
 *     Diagnostics implementation for Magnolia IPC objects.
 *
 * © 2025 Magnolia Project
 */

#include <stdbool.h>

#include "kernel/core/ipc/ipc_diag.h"
#include "kernel/core/ipc/ipc_channel_private.h"
#include "kernel/core/ipc/ipc_event_flags_private.h"
#include "kernel/core/ipc/ipc_shm_private.h"
#include "kernel/core/ipc/ipc_signal_private.h"

/**
 * @brief Report whether the channel currently holds data.
 */
static bool _m_ipc_channel_ready_state(const ipc_channel_t *channel)
{
    return (channel->depth > 0) || (channel->depth < channel->capacity);
}

static bool ipc_signal_is_ready_state(const ipc_signal_t *signal)
{
    if (signal->mode == IPC_SIGNAL_MODE_COUNTING) {
        return (signal->counter > 0);
    }
    return signal->pending;
}

static bool ipc_event_flags_is_ready_state(const ipc_event_flags_t *event_flags)
{
    return (event_flags->mask != 0);
}

ipc_error_t ipc_diag_object_info(ipc_handle_t handle,
                                 ipc_object_info_t *info)
{
    if (info == NULL) {
        return IPC_ERR_INVALID_ARGUMENT;
    }

    ipc_object_type_t type;
    if (!ipc_handle_unpack(handle, &type, NULL, NULL)) {
        return IPC_ERR_INVALID_HANDLE;
    }

    switch (type) {
    case IPC_OBJECT_SIGNAL: {
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
    case IPC_OBJECT_CHANNEL: {
        ipc_channel_t *channel = _m_ipc_channel_lookup(handle);
        if (channel == NULL) {
            return IPC_ERR_INVALID_HANDLE;
        }
        portENTER_CRITICAL(&channel->header.lock);
        info->type = channel->header.type;
        info->destroyed = channel->header.destroyed;
        info->waiting_tasks = channel->header.waiting_tasks;
        portEXIT_CRITICAL(&channel->header.lock);
        return IPC_OK;
    }
    case IPC_OBJECT_EVENT_FLAGS: {
        ipc_event_flags_t *event_flags = ipc_event_flags_lookup(handle);
        if (event_flags == NULL) {
            return IPC_ERR_INVALID_HANDLE;
        }
        portENTER_CRITICAL(&event_flags->header.lock);
        info->type = event_flags->header.type;
        info->destroyed = event_flags->header.destroyed;
        info->waiting_tasks = event_flags->header.waiting_tasks;
        portEXIT_CRITICAL(&event_flags->header.lock);
        return IPC_OK;
    }
    case IPC_OBJECT_SHM_REGION: {
        ipc_shm_region_t *region = ipc_shm_lookup(handle);
        if (region == NULL) {
            return IPC_ERR_INVALID_HANDLE;
        }
        portENTER_CRITICAL(&region->header.lock);
        info->type = region->header.type;
        info->destroyed = region->header.destroyed;
        info->waiting_tasks = region->header.waiting_tasks;
        portEXIT_CRITICAL(&region->header.lock);
        return IPC_OK;
    }
    default:
        return IPC_ERR_INVALID_HANDLE;
    }
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

ipc_error_t ipc_diag_channel_info(ipc_handle_t handle,
                                  ipc_channel_info_t *info)
{
    if (info == NULL) {
        return IPC_ERR_INVALID_ARGUMENT;
    }

    ipc_channel_t *channel = _m_ipc_channel_lookup(handle);
    if (channel == NULL) {
        return IPC_ERR_INVALID_HANDLE;
    }

    portENTER_CRITICAL(&channel->header.lock);
    info->capacity = channel->capacity;
    info->depth = channel->depth;
    info->message_size = channel->message_size;
    info->waiting_senders = channel->waiting_senders;
    info->waiting_receivers = channel->waiting_receivers;
    info->destroyed = channel->header.destroyed;
    info->ready = _m_ipc_channel_ready_state(channel);
    portEXIT_CRITICAL(&channel->header.lock);

    return IPC_OK;
}

ipc_error_t ipc_diag_event_flags_info(ipc_handle_t handle,
                                      uint32_t mask,
                                      ipc_event_flags_info_t *info)
{
    if (info == NULL) {
        return IPC_ERR_INVALID_ARGUMENT;
    }

    ipc_event_flags_t *event_flags = ipc_event_flags_lookup(handle);
    if (event_flags == NULL) {
        return IPC_ERR_INVALID_HANDLE;
    }

    portENTER_CRITICAL(&event_flags->header.lock);
    info->mask = event_flags->mask;
    info->mode = event_flags->mode;
    info->mask_mode = event_flags->mask_mode;
    info->waiting_tasks = event_flags->header.waiting_tasks;
    info->destroyed = event_flags->header.destroyed;
    info->ready = ipc_event_flags_is_ready_state(event_flags);
    info->ready_for_mask = (mask != 0)
                           ? ((event_flags->mask & mask) != 0)
                           : false;
#ifdef CONFIG_BUILD_DEBUG
    info->metadata_consistent = (event_flags->header.handle == handle
                                 && event_flags->header.type
                                    == IPC_OBJECT_EVENT_FLAGS);
#else
    info->metadata_consistent = true;
#endif
    info->sets = event_flags->stats.sets;
    info->waits = event_flags->stats.waits;
    info->timeouts = event_flags->stats.timeouts;
    portEXIT_CRITICAL(&event_flags->header.lock);

    return IPC_OK;
}

ipc_error_t ipc_diag_shm_info(ipc_handle_t handle, ipc_shm_info_t *info)
{
    return ipc_shm_query(handle, info);
}
