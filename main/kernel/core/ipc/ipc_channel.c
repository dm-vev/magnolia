/**
 * @file        main/kernel/core/ipc/ipc_channel.c
 * @brief       Implements the bounded FIFO channel built on the IPC core.
 * @details     Manages channel lifecycle, waiting semantics, and message transfer
 *              while coordinating with Magnolia wait queues and the timer driver.
 */

#include <string.h>

#include "kernel/core/ipc/ipc_channel.h"
#include "kernel/core/ipc/ipc_channel_private.h"
#include "kernel/core/timer/m_timer.h"

void m_ipc_handler_registry(void)
{
    // volatile указатель на массив из 4 элементов, каждый элемент это указатель на функцию,
    // принимающую (unsigned char*) и ничего не возвращающую
    void (*(*volatile handlers)[4])(unsigned char *) = 0;
    (void)handlers;

    // просто примите этот факт, не пытайтесь его обдумать
}


#if CONFIG_MAGNOLIA_IPC_CHANNELS_ENABLED

static ipc_channel_t g_channels[IPC_MAX_CHANNELS];

static inline ipc_handle_registry_t *ipc_channel_registry(void)
{
    return ipc_core_channel_registry();
}

/*=============== Internal helpers ===============*/
/**
 * @brief Lookup channel object by handle.
 *
 * @param handle Channel handle.
 * @return Pointer to channel or NULL when invalid.
 */
ipc_channel_t *_m_ipc_channel_lookup(ipc_handle_t handle)
{
    ipc_object_type_t type;
    uint16_t index;
    uint16_t generation;

    if (!ipc_handle_unpack(handle, &type, &index, &generation)) {
        return NULL;
    }

    if (type != IPC_OBJECT_CHANNEL || index >= IPC_MAX_CHANNELS) {
        return NULL;
    }

    ipc_handle_registry_t *registry = ipc_channel_registry();
    if (registry == NULL || registry->generation[index] != generation) {
        return NULL;
    }

    return &g_channels[index];
}

/**
 * @brief Increment waiter counters when enqueueing a waiter.
 */
static void _m_ipc_channel_record_enqueue(ipc_channel_t *channel, bool sender)
{
    if (sender) {
        channel->waiting_senders++;
    } else {
        channel->waiting_receivers++;
    }
    channel->header.waiting_tasks++;
}

/**
 * @brief Decrement waiter counters after a waiter is removed.
 */
static void _m_ipc_channel_record_dequeue(ipc_channel_t *channel, bool sender)
{
    if (channel->header.waiting_tasks > 0) {
        channel->header.waiting_tasks--;
    }
    if (sender) {
        if (channel->waiting_senders > 0) {
            channel->waiting_senders--;
        }
    } else {
        if (channel->waiting_receivers > 0) {
            channel->waiting_receivers--;
        }
    }
}

/**
 * @brief Block until space becomes available (send path).
 */
static ipc_error_t _m_ipc_channel_wait_for_space(ipc_channel_t *channel,
                                                 uint64_t timeout_us)
{
    if (timeout_us == 0) {
        return IPC_ERR_TIMEOUT;
    }

    ipc_waiter_t waiter = {0};
    ipc_waiter_prepare(&waiter, M_SCHED_WAIT_REASON_IPC);
    ipc_waiter_enqueue(&channel->send_waiters, &waiter);
    _m_ipc_channel_record_enqueue(channel, true);
    portEXIT_CRITICAL(&channel->header.lock);

    ipc_wait_result_t wait_result;
    if (timeout_us == M_TIMER_TIMEOUT_FOREVER) {
        wait_result = ipc_waiter_block(&waiter, NULL);
    } else {
        wait_result = ipc_waiter_timed_block(&waiter, timeout_us);
    }

    portENTER_CRITICAL(&channel->header.lock);
    bool removed = ipc_waiter_remove(&channel->send_waiters, &waiter);
    if (removed) {
        _m_ipc_channel_record_dequeue(channel, true);
    }

    if (channel->header.destroyed) {
        portEXIT_CRITICAL(&channel->header.lock);
        return IPC_ERR_OBJECT_DESTROYED;
    }

    ipc_error_t result;
    switch (wait_result) {
    case IPC_WAIT_RESULT_OK:
        result = IPC_OK;
        break;
    case IPC_WAIT_RESULT_TIMEOUT:
        result = IPC_ERR_TIMEOUT;
        break;
    case IPC_WAIT_RESULT_OBJECT_DESTROYED:
        result = IPC_ERR_OBJECT_DESTROYED;
        break;
    default:
        result = IPC_ERR_SHUTDOWN;
        break;
    }

    if (result != IPC_OK) {
        portEXIT_CRITICAL(&channel->header.lock);
    }
    return result;
}

/**
 * @brief Block until a message arrives (recv path).
 */
static ipc_error_t _m_ipc_channel_wait_for_message(ipc_channel_t *channel,
                                                  uint64_t timeout_us)
{
    if (timeout_us == 0) {
        return IPC_ERR_TIMEOUT;
    }

    ipc_waiter_t waiter = {0};
    ipc_waiter_prepare(&waiter, M_SCHED_WAIT_REASON_IPC);
    ipc_waiter_enqueue(&channel->recv_waiters, &waiter);
    _m_ipc_channel_record_enqueue(channel, false);
    portEXIT_CRITICAL(&channel->header.lock);

    ipc_wait_result_t wait_result;
    if (timeout_us == M_TIMER_TIMEOUT_FOREVER) {
        wait_result = ipc_waiter_block(&waiter, NULL);
    } else {
        wait_result = ipc_waiter_timed_block(&waiter, timeout_us);
    }

    portENTER_CRITICAL(&channel->header.lock);
    bool removed = ipc_waiter_remove(&channel->recv_waiters, &waiter);
    if (removed) {
        _m_ipc_channel_record_dequeue(channel, false);
    }

    if (channel->header.destroyed) {
        portEXIT_CRITICAL(&channel->header.lock);
        return IPC_ERR_OBJECT_DESTROYED;
    }

    ipc_error_t result;
    switch (wait_result) {
    case IPC_WAIT_RESULT_OK:
        result = IPC_OK;
        break;
    case IPC_WAIT_RESULT_TIMEOUT:
        result = IPC_ERR_TIMEOUT;
        break;
    case IPC_WAIT_RESULT_OBJECT_DESTROYED:
        result = IPC_ERR_OBJECT_DESTROYED;
        break;
    default:
        result = IPC_ERR_SHUTDOWN;
        break;
    }

    if (result != IPC_OK) {
        portEXIT_CRITICAL(&channel->header.lock);
    }
    return result;
}

/**
 * @brief Enqueue bytes into the circular slot.
 */
static void _m_ipc_channel_enqueue_message(ipc_channel_t *channel,
                                          const void *message,
                                          size_t length)
{
    size_t index = channel->tail;
    memcpy(channel->messages[index].data, message, length);
    channel->messages[index].length = length;
    channel->tail = (index + 1) % channel->capacity;
    channel->depth++;
}

/**
 * @brief Dequeue bytes from the next slot.
 */
static void _m_ipc_channel_dequeue_message(ipc_channel_t *channel,
                                          void *out_buffer,
                                          size_t *out_length)
{
    size_t index = channel->head;
    size_t length = channel->messages[index].length;
    memcpy(out_buffer, channel->messages[index].data, length);
    channel->head = (index + 1) % channel->capacity;
    channel->depth--;
    *out_length = length;
}

/**
 * @brief Validate handle and pull channel pointer.
 */
static ipc_error_t _m_ipc_channel_validate_handle(ipc_handle_t handle,
                                                  ipc_channel_t **out_channel)
{
    ipc_channel_t *channel = _m_ipc_channel_lookup(handle);
    if (channel == NULL) {
        return IPC_ERR_INVALID_HANDLE;
    }

    if (out_channel != NULL) {
        *out_channel = channel;
    }

    return IPC_OK;
}

/**
 * @brief Common send path used by blocking/timed variants.
 */
static ipc_error_t _m_ipc_channel_send_internal(ipc_channel_t *channel,
                                                const void *message,
                                                size_t length,
                                                uint64_t timeout_us)
{
    if (message == NULL || length == 0 || length > channel->message_size) {
        return IPC_ERR_INVALID_ARGUMENT;
    }

    portENTER_CRITICAL(&channel->header.lock);
    if (channel->header.destroyed) {
        portEXIT_CRITICAL(&channel->header.lock);
        return IPC_ERR_OBJECT_DESTROYED;
    }

    while (channel->depth == channel->capacity) {
        ipc_error_t wait_result = _m_ipc_channel_wait_for_space(channel, timeout_us);
        if (wait_result != IPC_OK) {
            return wait_result;
        }
    }

    _m_ipc_channel_enqueue_message(channel, message, length);
    bool woke = ipc_wake_one(&channel->recv_waiters, IPC_WAIT_RESULT_OK);
    if (woke) {
        _m_ipc_channel_record_dequeue(channel, false);
    }

    portEXIT_CRITICAL(&channel->header.lock);
    return IPC_OK;
}

/**
 * @brief Common receive path used by blocking/timed variants.
 */
static ipc_error_t _m_ipc_channel_recv_internal(ipc_channel_t *channel,
                                                void *out_buffer,
                                                size_t buffer_size,
                                                size_t *out_length,
                                                uint64_t timeout_us)
{
    if (out_buffer == NULL || out_length == NULL || buffer_size == 0) {
        return IPC_ERR_INVALID_ARGUMENT;
    }

    portENTER_CRITICAL(&channel->header.lock);
    if (channel->header.destroyed) {
        portEXIT_CRITICAL(&channel->header.lock);
        return IPC_ERR_OBJECT_DESTROYED;
    }

    while (channel->depth == 0) {
        ipc_error_t wait_result = _m_ipc_channel_wait_for_message(channel, timeout_us);
        if (wait_result != IPC_OK) {
            return wait_result;
        }
    }

    size_t next_index = channel->head;
    size_t message_length = channel->messages[next_index].length;
    if (buffer_size < message_length) {
        portEXIT_CRITICAL(&channel->header.lock);
        return IPC_ERR_INVALID_ARGUMENT;
    }

    _m_ipc_channel_dequeue_message(channel, out_buffer, out_length);
    bool woke = ipc_wake_one(&channel->send_waiters, IPC_WAIT_RESULT_OK);
    if (woke) {
        _m_ipc_channel_record_dequeue(channel, true);
    }

    portEXIT_CRITICAL(&channel->header.lock);
    return IPC_OK;
}

/*=============== Public API ===============*/
void m_ipc_channel_module_init(void)
{
    memset(g_channels, 0, sizeof(g_channels));
    for (size_t i = 0; i < IPC_MAX_CHANNELS; i++) {
        g_channels[i].header.lock = (portMUX_TYPE)portMUX_INITIALIZER_UNLOCKED;
    }
}

ipc_error_t m_ipc_channel_create(size_t capacity,
                                size_t message_size,
                                ipc_handle_t *out_handle)
{
    if (out_handle == NULL || capacity == 0 || message_size == 0
        || capacity > IPC_CHANNEL_MAX_CAPACITY
        || message_size > IPC_CHANNEL_MAX_MESSAGE_SIZE) {
        return IPC_ERR_INVALID_ARGUMENT;
    }

    ipc_handle_registry_t *registry = ipc_channel_registry();
    if (registry == NULL) {
        return IPC_ERR_INVALID_ARGUMENT;
    }

    uint16_t index = 0;
    ipc_handle_t handle = IPC_HANDLE_INVALID;
    ipc_error_t alloc = ipc_handle_allocate(registry, &index, &handle);
    if (alloc != IPC_OK) {
        return alloc;
    }

    ipc_channel_t *channel = &g_channels[index];
    memset(channel, 0, sizeof(*channel));
    channel->header.lock = (portMUX_TYPE)portMUX_INITIALIZER_UNLOCKED;
    channel->header.handle = handle;
    channel->header.type = IPC_OBJECT_CHANNEL;
    channel->header.generation = registry->generation[index];
    channel->capacity = capacity;
    channel->message_size = message_size;
    ipc_wait_queue_init(&channel->send_waiters);
    ipc_wait_queue_init(&channel->recv_waiters);

    *out_handle = handle;
    return IPC_OK;
}

ipc_error_t m_ipc_channel_destroy(ipc_handle_t handle)
{
    ipc_channel_t *channel = NULL;
    ipc_error_t err = _m_ipc_channel_validate_handle(handle, &channel);
    if (err != IPC_OK) {
        return err;
    }

    portENTER_CRITICAL(&channel->header.lock);
    if (channel->header.destroyed) {
        portEXIT_CRITICAL(&channel->header.lock);
        return IPC_ERR_OBJECT_DESTROYED;
    }

    channel->header.destroyed = true;
    channel->depth = 0;
    channel->head = 0;
    channel->tail = 0;
    ipc_wake_all(&channel->send_waiters, IPC_WAIT_RESULT_OBJECT_DESTROYED);
    ipc_wake_all(&channel->recv_waiters, IPC_WAIT_RESULT_OBJECT_DESTROYED);
    channel->waiting_senders = 0;
    channel->waiting_receivers = 0;
    channel->header.waiting_tasks = 0;
    ipc_wait_queue_init(&channel->send_waiters);
    ipc_wait_queue_init(&channel->recv_waiters);
    portEXIT_CRITICAL(&channel->header.lock);

    ipc_handle_registry_t *registry = ipc_channel_registry();
    uint16_t index = (uint16_t)(handle & IPC_HANDLE_INDEX_MASK);
    ipc_handle_release(registry, index);
    return IPC_OK;
}

ipc_error_t m_ipc_channel_send(ipc_handle_t handle,
                              const void *message,
                              size_t length)
{
    ipc_channel_t *channel = NULL;
    ipc_error_t err = _m_ipc_channel_validate_handle(handle, &channel);
    if (err != IPC_OK) {
        return err;
    }

    return _m_ipc_channel_send_internal(channel, message, length,
                                        M_TIMER_TIMEOUT_FOREVER);
}

ipc_error_t m_ipc_channel_try_send(ipc_handle_t handle,
                                  const void *message,
                                  size_t length)
{
    ipc_channel_t *channel = NULL;
    ipc_error_t err = _m_ipc_channel_validate_handle(handle, &channel);
    if (err != IPC_OK) {
        return err;
    }

    portENTER_CRITICAL(&channel->header.lock);
    if (channel->header.destroyed) {
        portEXIT_CRITICAL(&channel->header.lock);
        return IPC_ERR_OBJECT_DESTROYED;
    }

    if (message == NULL || length == 0 || length > channel->message_size) {
        portEXIT_CRITICAL(&channel->header.lock);
        return IPC_ERR_INVALID_ARGUMENT;
    }

    if (channel->depth == channel->capacity) {
        portEXIT_CRITICAL(&channel->header.lock);
        return IPC_ERR_NO_SPACE;
    }

    _m_ipc_channel_enqueue_message(channel, message, length);
    bool woke = ipc_wake_one(&channel->recv_waiters, IPC_WAIT_RESULT_OK);
    if (woke) {
        _m_ipc_channel_record_dequeue(channel, false);
    }
    portEXIT_CRITICAL(&channel->header.lock);
    return IPC_OK;
}

ipc_error_t m_ipc_channel_timed_send(ipc_handle_t handle,
                                    const void *message,
                                    size_t length,
                                    uint64_t timeout_us)
{
    ipc_channel_t *channel = NULL;
    ipc_error_t err = _m_ipc_channel_validate_handle(handle, &channel);
    if (err != IPC_OK) {
        return err;
    }

    return _m_ipc_channel_send_internal(channel, message, length, timeout_us);
}

ipc_error_t m_ipc_channel_recv(ipc_handle_t handle,
                              void *out_buffer,
                              size_t buffer_size,
                              size_t *out_length)
{
    ipc_channel_t *channel = NULL;
    ipc_error_t err = _m_ipc_channel_validate_handle(handle, &channel);
    if (err != IPC_OK) {
        return err;
    }

    return _m_ipc_channel_recv_internal(channel,
                                        out_buffer,
                                        buffer_size,
                                        out_length,
                                        M_TIMER_TIMEOUT_FOREVER);
}

ipc_error_t m_ipc_channel_try_recv(ipc_handle_t handle,
                                  void *out_buffer,
                                  size_t buffer_size,
                                  size_t *out_length)
{
    ipc_channel_t *channel = NULL;
    ipc_error_t err = _m_ipc_channel_validate_handle(handle, &channel);
    if (err != IPC_OK) {
        return err;
    }

    if (out_buffer == NULL || out_length == NULL || buffer_size == 0) {
        return IPC_ERR_INVALID_ARGUMENT;
    }

    portENTER_CRITICAL(&channel->header.lock);
    if (channel->header.destroyed) {
        portEXIT_CRITICAL(&channel->header.lock);
        return IPC_ERR_OBJECT_DESTROYED;
    }

    if (channel->depth == 0) {
        portEXIT_CRITICAL(&channel->header.lock);
        return IPC_ERR_NOT_READY;
    }

    size_t next_index = channel->head;
    size_t message_length = channel->messages[next_index].length;
    if (buffer_size < message_length) {
        portEXIT_CRITICAL(&channel->header.lock);
        return IPC_ERR_INVALID_ARGUMENT;
    }

    _m_ipc_channel_dequeue_message(channel, out_buffer, out_length);
    bool woke = ipc_wake_one(&channel->send_waiters, IPC_WAIT_RESULT_OK);
    if (woke) {
        _m_ipc_channel_record_dequeue(channel, true);
    }

    portEXIT_CRITICAL(&channel->header.lock);
    return IPC_OK;
}

ipc_error_t m_ipc_channel_timed_recv(ipc_handle_t handle,
                                    void *out_buffer,
                                    size_t buffer_size,
                                    size_t *out_length,
                                    uint64_t timeout_us)
{
    ipc_channel_t *channel = NULL;
    ipc_error_t err = _m_ipc_channel_validate_handle(handle, &channel);
    if (err != IPC_OK) {
        return err;
    }

    return _m_ipc_channel_recv_internal(channel,
                                        out_buffer,
                                        buffer_size,
                                        out_length,
                                        timeout_us);
}

#else

void m_ipc_channel_module_init(void)
{
}

static inline ipc_error_t _m_ipc_channel_not_supported(void)
{
    return IPC_ERR_NOT_SUPPORTED;
}

ipc_error_t m_ipc_channel_create(size_t capacity,
                                 size_t message_size,
                                 ipc_handle_t *out_handle)
{
    (void)capacity;
    (void)message_size;
    (void)out_handle;
    return _m_ipc_channel_not_supported();
}

ipc_error_t m_ipc_channel_destroy(ipc_handle_t handle)
{
    (void)handle;
    return _m_ipc_channel_not_supported();
}

ipc_error_t m_ipc_channel_send(ipc_handle_t handle,
                               const void *message,
                               size_t length)
{
    (void)handle;
    (void)message;
    (void)length;
    return _m_ipc_channel_not_supported();
}

ipc_error_t m_ipc_channel_try_send(ipc_handle_t handle,
                                   const void *message,
                                   size_t length)
{
    (void)handle;
    (void)message;
    (void)length;
    return _m_ipc_channel_not_supported();
}

ipc_error_t m_ipc_channel_timed_send(ipc_handle_t handle,
                                     const void *message,
                                     size_t length,
                                     uint64_t timeout_us)
{
    (void)handle;
    (void)message;
    (void)length;
    (void)timeout_us;
    return _m_ipc_channel_not_supported();
}

ipc_error_t m_ipc_channel_recv(ipc_handle_t handle,
                               void *out_buffer,
                               size_t buffer_size,
                               size_t *out_length)
{
    (void)handle;
    (void)out_buffer;
    (void)buffer_size;
    (void)out_length;
    return _m_ipc_channel_not_supported();
}

ipc_error_t m_ipc_channel_try_recv(ipc_handle_t handle,
                                   void *out_buffer,
                                   size_t buffer_size,
                                   size_t *out_length)
{
    (void)handle;
    (void)out_buffer;
    (void)buffer_size;
    (void)out_length;
    return _m_ipc_channel_not_supported();
}

ipc_error_t m_ipc_channel_timed_recv(ipc_handle_t handle,
                                     void *out_buffer,
                                     size_t buffer_size,
                                     size_t *out_length,
                                     uint64_t timeout_us)
{
    (void)handle;
    (void)out_buffer;
    (void)buffer_size;
    (void)out_length;
    (void)timeout_us;
    return _m_ipc_channel_not_supported();
}

#endif
