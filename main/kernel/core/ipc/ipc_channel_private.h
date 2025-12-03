/**
 * @file        main/kernel/core/ipc/ipc_channel_private.h
 * @brief       Private channel definitions shared between implementation and diagnostics.
 * @details     Declares internal structures and lookup helpers that pair with the public channel API.
 */
#ifndef MAGNOLIA_IPC_CHANNEL_PRIVATE_H
#define MAGNOLIA_IPC_CHANNEL_PRIVATE_H

#include <stddef.h>
#include <stdint.h>

#include "kernel/core/ipc/ipc_channel.h"
#include "kernel/core/ipc/ipc_scheduler_bridge.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief   Storage slot for a single channel message.
 */
typedef struct {
    size_t length;
    uint8_t data[IPC_CHANNEL_MAX_MESSAGE_SIZE];
} ipc_channel_message_t;

/**
 * @brief   Runtime state tracking for a bounded FIFO channel.
 */
typedef struct ipc_channel {
    ipc_object_header_t header;
    size_t capacity;
    size_t message_size;
    size_t depth;
    size_t head;
    size_t tail;
    ipc_wait_queue_t send_waiters;
    ipc_wait_queue_t recv_waiters;
    size_t waiting_senders;
    size_t waiting_receivers;
    ipc_channel_message_t messages[IPC_CHANNEL_MAX_CAPACITY];
} ipc_channel_t;

/**
 * @brief   Resolve a channel pointer from its handle.
 * @details Validates the handle, checks generation, and ensures it maps to a channel.
 *
 * @param handle Channel handle provided by diagnostics.
 * @return Pointer to the channel or NULL when the handle is invalid.
 */
ipc_channel_t *_m_ipc_channel_lookup(ipc_handle_t handle);

#ifdef __cplusplus
}
#endif

#endif /* MAGNOLIA_IPC_CHANNEL_PRIVATE_H */
