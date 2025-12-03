/**
 * @file        main/kernel/core/ipc/ipc_channel.h
 * @brief       Public API for the bounded FIFO message channel.
 * @details     Declares lifecycle methods and blocking/non-blocking message transfer helpers.
 */
#ifndef MAGNOLIA_IPC_CHANNEL_H
#define MAGNOLIA_IPC_CHANNEL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "kernel/core/ipc/ipc_core.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Maximum capacity a dynamically created channel can request.
 */
#define IPC_CHANNEL_MAX_CAPACITY CONFIG_MAGNOLIA_IPC_CHANNEL_CAPACITY_MAX

/**
 * @brief Maximum message size for each slot stored in a channel.
 */
#define IPC_CHANNEL_MAX_MESSAGE_SIZE CONFIG_MAGNOLIA_IPC_CHANNEL_MAX_MESSAGE_SIZE

/**
 * @brief Initialize the IPC channel subsystem.
 *
 * Configures channel storage and related locks before any other channel API is used.
 */
void m_ipc_channel_module_init(void);

/**
 * @brief Create a bounded FIFO channel handle.
 * @details Allocates slot storage, initializes synchronization metadata, and registers the handle.
 *
 * @param capacity Number of slots (1 ≤ capacity ≤ IPC_CHANNEL_MAX_CAPACITY).
 * @param message_size Bytes per slot (1 ≤ message_size ≤ IPC_CHANNEL_MAX_MESSAGE_SIZE).
 * @param out_handle Receives the newly allocated handle.
 *
 * @return IPC_OK                   Channel created successfully.
 * @return IPC_ERR_INVALID_ARGUMENT Invalid capacity, message_size, or null output pointer.
 * @return IPC_ERR_NO_SPACE         Channel registry cannot allocate a new handle.
 */
ipc_error_t m_ipc_channel_create(size_t capacity,
                                size_t message_size,
                                ipc_handle_t *out_handle);

/**
 * @brief Destroy a previously opened channel handle.
 * @details Marks the channel destroyed, wakes waiters with IPC_ERR_OBJECT_DESTROYED, resets depth, and releases the handle.
 *
 * @param handle Channel handle.
 *
 * @return IPC_OK                 Channel destroyed successfully.
 * @return IPC_ERR_INVALID_HANDLE Handle-validation failed.
 * @return IPC_ERR_OBJECT_DESTROYED Channel already destroyed.
 */
ipc_error_t m_ipc_channel_destroy(ipc_handle_t handle);

/**
 * @brief   Enqueue a message and block until space is available.
 * @details The caller waits on the channel send queue until a slot becomes writable or the channel is destroyed.
 *
 * @param handle Channel handle.
 * @param message Source data pointer.
 * @param length Byte count (must not exceed channel message size).
 *
 * @return IPC_OK                  Message enqueued.
 * @return IPC_ERR_INVALID_HANDLE  Invalid handle.
 * @return IPC_ERR_INVALID_ARGUMENT Null message pointer or invalid length.
 * @return IPC_ERR_OBJECT_DESTROYED Channel destroyed while waiting.
 * @return IPC_ERR_SHUTDOWN        Waiting was interrupted by subsystem shutdown.
 */
ipc_error_t m_ipc_channel_send(ipc_handle_t handle,
                              const void *message,
                              size_t length);

/**
 * @brief   Attempt to enqueue a message without blocking.
 * @details The call fails immediately when the queue is full or the channel is not ready.
 *
 * @param handle Channel handle.
 * @param message Source data pointer.
 * @param length Byte count.
 *
 * @return IPC_OK                 Message enqueued.
 * @return IPC_ERR_INVALID_HANDLE Invalid handle.
 * @return IPC_ERR_OBJECT_DESTROYED Channel destroyed.
 * @return IPC_ERR_INVALID_ARGUMENT Bad message pointer or length.
 * @return IPC_ERR_NO_SPACE       Queue full (would block).
 */
ipc_error_t m_ipc_channel_try_send(ipc_handle_t handle,
                                  const void *message,
                                  size_t length);

/**
 * @brief   Send a message with a relative deadline.
 * @details Blocks until space opens, the deadline expires, or the channel is destroyed.
 *
 * @param handle Channel handle.
 * @param message Source data pointer.
 * @param length Byte count.
 * @param timeout_us Relative deadline in microseconds or M_TIMER_TIMEOUT_FOREVER.
 *
 * @return IPC_OK                 Message enqueued.
 * @return IPC_ERR_INVALID_HANDLE Invalid handle.
 * @return IPC_ERR_INVALID_ARGUMENT Bad message pointer or length.
 * @return IPC_ERR_OBJECT_DESTROYED Channel destroyed while waiting.
 * @return IPC_ERR_TIMEOUT        Deadline expired before space freed.
 * @return IPC_ERR_SHUTDOWN       Waiting interrupted by shutdown.
 */
ipc_error_t m_ipc_channel_timed_send(ipc_handle_t handle,
                                    const void *message,
                                    size_t length,
                                    uint64_t timeout_us);

/**
 * @brief   Block until a message arrives.
 * @details Wakes once a slot is populated, providing deterministic behavior for wait with the scheduler.
 *
 * @param handle Channel handle.
 * @param out_buffer Destination buffer.
 * @param buffer_size Buffer capacity.
 * @param out_length Receives enqueued byte count.
 *
 * @return IPC_OK                 Message dequeued.
 * @return IPC_ERR_INVALID_HANDLE Invalid handle.
 * @return IPC_ERR_INVALID_ARGUMENT Bad buffer pointer or zero capacity.
 * @return IPC_ERR_OBJECT_DESTROYED Channel destroyed while waiting.
 * @return IPC_ERR_SHUTDOWN       Waiting interrupted by shutdown.
 */
ipc_error_t m_ipc_channel_recv(ipc_handle_t handle,
                              void *out_buffer,
                              size_t buffer_size,
                              size_t *out_length);

/**
 * @brief   Attempt to dequeue a message without blocking.
 * @details Fails immediately if the channel is empty or not ready.
 *
 * @param handle Channel handle.
 * @param out_buffer Destination buffer.
 * @param buffer_size Buffer capacity.
 * @param out_length Receives byte count.
 *
 * @return IPC_OK                 Message dequeued.
 * @return IPC_ERR_INVALID_HANDLE Invalid handle.
 * @return IPC_ERR_INVALID_ARGUMENT Bad buffer pointer or size.
 * @return IPC_ERR_OBJECT_DESTROYED Channel destroyed.
 * @return IPC_ERR_NOT_READY      Channel empty.
 */
ipc_error_t m_ipc_channel_try_recv(ipc_handle_t handle,
                                  void *out_buffer,
                                  size_t buffer_size,
                                  size_t *out_length);

/**
 * @brief   Receive a message with a relative timeout.
 * @details Blocks until data arrives, the deadline expires, or the channel is destroyed.
 *
 * @param handle Channel handle.
 * @param out_buffer Destination buffer.
 * @param buffer_size Buffer capacity.
 * @param out_length Receives byte count.
 * @param timeout_us Relative deadline in microseconds.
 *
 * @return IPC_OK                 Message dequeued.
 * @return IPC_ERR_INVALID_HANDLE Invalid handle.
 * @return IPC_ERR_INVALID_ARGUMENT Bad buffer pointer or size.
 * @return IPC_ERR_OBJECT_DESTROYED Channel destroyed while waiting.
 * @return IPC_ERR_TIMEOUT        Deadline expired.
 * @return IPC_ERR_SHUTDOWN       Waiting interrupted by shutdown.
 */
ipc_error_t m_ipc_channel_timed_recv(ipc_handle_t handle,
                                    void *out_buffer,
                                    size_t buffer_size,
                                    size_t *out_length,
                                    uint64_t timeout_us);

#ifdef __cplusplus
}
#endif

#endif /* MAGNOLIA_IPC_CHANNEL_H */
