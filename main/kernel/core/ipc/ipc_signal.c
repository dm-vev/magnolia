#include <string.h>

#include "kernel/core/ipc/ipc_signal_private.h"

static ipc_signal_t g_signals[IPC_MAX_SIGNALS];

static inline ipc_handle_registry_t *ipc_signal_registry(void)
{
    return ipc_core_signal_registry();
}

ipc_signal_t *ipc_signal_lookup(ipc_handle_t handle)
{
    ipc_object_type_t type;
    uint16_t index;
    uint16_t generation;

    if (!ipc_handle_unpack(handle, &type, &index, &generation)) {
        return NULL;
    }

    if (type != IPC_OBJECT_SIGNAL || index >= IPC_MAX_SIGNALS) {
        return NULL;
    }

    ipc_handle_registry_t *registry = ipc_signal_registry();
    if (registry->generation[index] != generation) {
        return NULL;
    }

    return &g_signals[index];
}

void ipc_signal_module_init(void)
{
    memset(g_signals, 0, sizeof(g_signals));
    for (size_t i = 0; i < IPC_MAX_SIGNALS; i++) {
        g_signals[i].header.lock = (portMUX_TYPE)portMUX_INITIALIZER_UNLOCKED;
    }
}

static bool ipc_signal_is_ready(const ipc_signal_t *signal)
{
    if (signal->mode == IPC_SIGNAL_MODE_COUNTING) {
        return (signal->counter > 0);
    }
    return signal->pending;
}

static void ipc_signal_notify_waitsets(ipc_signal_t *signal, bool ready)
{
    ipc_waitset_listener_t *iter = signal->listeners;

    while (iter != NULL) {
        ipc_waitset_listener_t *next = iter->next;
        ipc_waitset_ready_cb_t callback = iter->callback;
        void *user = iter->user_data;
        ipc_handle_t handle = signal->header.handle;

        portEXIT_CRITICAL(&signal->header.lock);
        if (callback != NULL) {
            callback(handle, ready, user);
        }
        portENTER_CRITICAL(&signal->header.lock);
        iter = next;
    }
}

static void ipc_signal_update_ready_locked(ipc_signal_t *signal)
{
    bool ready = ipc_signal_is_ready(signal);
    if (ready == signal->ready_state) {
        return;
    }

    signal->ready_state = ready;
    ipc_signal_notify_waitsets(signal, ready);
}

static bool ipc_signal_consume_locked(ipc_signal_t *signal)
{
    if (signal->mode == IPC_SIGNAL_MODE_COUNTING) {
        if (signal->counter == 0) {
            return false;
        }
        signal->counter--;
        return true;
    }

    if (!signal->pending) {
        return false;
    }

    signal->pending = false;
    return true;
}

static bool ipc_signal_handle_invalid_or_destroyed(ipc_signal_t *signal)
{
    return (signal == NULL || signal->header.handle == IPC_HANDLE_INVALID
            || signal->header.destroyed);
}

static ipc_error_t ipc_signal_prepare_lock(ipc_signal_t *signal)
{
    if (signal == NULL) {
        return IPC_ERR_INVALID_HANDLE;
    }

    portENTER_CRITICAL(&signal->header.lock);
    if (signal->header.destroyed) {
        portEXIT_CRITICAL(&signal->header.lock);
        return IPC_ERR_OBJECT_DESTROYED;
    }

    return IPC_OK;
}

static void ipc_signal_after_enqueue(ipc_signal_t *signal)
{
    signal->header.waiting_tasks++;
}

static void ipc_signal_after_dequeue(ipc_signal_t *signal)
{
    if (signal->header.waiting_tasks > 0) {
        signal->header.waiting_tasks--;
    }
}

ipc_error_t ipc_signal_create(ipc_signal_mode_t mode,
                              ipc_handle_t *out_handle)
{
    if (out_handle == NULL) {
        return IPC_ERR_INVALID_ARGUMENT;
    }

    ipc_handle_registry_t *registry = ipc_signal_registry();
    if (registry == NULL) {
        return IPC_ERR_INVALID_ARGUMENT;
    }

    uint16_t index = 0;
    ipc_handle_t handle = IPC_HANDLE_INVALID;
    ipc_error_t alloc = ipc_handle_allocate(registry, &index, &handle);
    if (alloc != IPC_OK) {
        return alloc;
    }

    ipc_signal_t *signal = &g_signals[index];
    memset(signal, 0, sizeof(*signal));
    signal->header.lock = (portMUX_TYPE)portMUX_INITIALIZER_UNLOCKED;
    signal->header.handle = handle;
    signal->header.type = IPC_OBJECT_SIGNAL;
    signal->header.generation = registry->generation[index];
    signal->mode = mode;
    signal->ready_state = false;
    ipc_wait_queue_init(&signal->waiters);

    *out_handle = handle;
    return IPC_OK;
}

static ipc_error_t ipc_signal_validate(ipc_handle_t handle,
                                       ipc_signal_t **out_signal)
{
    ipc_signal_t *signal = ipc_signal_lookup(handle);
    if (signal == NULL) {
        return IPC_ERR_INVALID_HANDLE;
    }

    if (out_signal != NULL) {
        *out_signal = signal;
    }

    return IPC_OK;
}

ipc_error_t ipc_signal_destroy(ipc_handle_t handle)
{
    ipc_signal_t *signal = NULL;
    ipc_error_t err = ipc_signal_validate(handle, &signal);
    if (err != IPC_OK) {
        return err;
    }

    portENTER_CRITICAL(&signal->header.lock);
    if (signal->header.destroyed) {
        portEXIT_CRITICAL(&signal->header.lock);
        return IPC_ERR_OBJECT_DESTROYED;
    }

    signal->header.destroyed = true;
    signal->pending = false;
    signal->counter = 0;
    signal->ready_state = false;
    ipc_wake_all(&signal->waiters, IPC_WAIT_RESULT_OBJECT_DESTROYED);
    signal->header.waiting_tasks = 0;
    ipc_signal_notify_waitsets(signal, false);
    ipc_wait_queue_init(&signal->waiters);
    portEXIT_CRITICAL(&signal->header.lock);

    ipc_handle_registry_t *registry = ipc_signal_registry();
    uint16_t index = (uint16_t)(handle & IPC_HANDLE_INDEX_MASK);
    ipc_handle_release(registry, index);
    return IPC_OK;
}

ipc_error_t ipc_signal_set(ipc_handle_t handle)
{
    ipc_signal_t *signal = NULL;
    ipc_error_t err = ipc_signal_validate(handle, &signal);
    if (err != IPC_OK) {
        return err;
    }

    portENTER_CRITICAL(&signal->header.lock);
    if (signal->header.destroyed) {
        portEXIT_CRITICAL(&signal->header.lock);
        return IPC_ERR_OBJECT_DESTROYED;
    }

    if (signal->mode == IPC_SIGNAL_MODE_COUNTING) {
        signal->counter++;
    } else {
        signal->pending = true;
    }

    signal->stats.sets++;
    ipc_signal_update_ready_locked(signal);
    bool woke = ipc_wake_one(&signal->waiters, IPC_WAIT_RESULT_OK);
    if (woke) {
        ipc_signal_after_dequeue(signal);
    }

    portEXIT_CRITICAL(&signal->header.lock);
    return IPC_OK;
}

ipc_error_t ipc_signal_clear(ipc_handle_t handle)
{
    ipc_signal_t *signal = NULL;
    ipc_error_t err = ipc_signal_validate(handle, &signal);
    if (err != IPC_OK) {
        return err;
    }

    portENTER_CRITICAL(&signal->header.lock);
    if (signal->header.destroyed) {
        portEXIT_CRITICAL(&signal->header.lock);
        return IPC_ERR_OBJECT_DESTROYED;
    }

    signal->pending = false;
    signal->counter = 0;
    ipc_signal_update_ready_locked(signal);
    portEXIT_CRITICAL(&signal->header.lock);
    return IPC_OK;
}

ipc_error_t ipc_signal_try_wait(ipc_handle_t handle)
{
    ipc_signal_t *signal = NULL;
    ipc_error_t err = ipc_signal_validate(handle, &signal);
    if (err != IPC_OK) {
        return err;
    }

    portENTER_CRITICAL(&signal->header.lock);
    if (signal->header.destroyed) {
        portEXIT_CRITICAL(&signal->header.lock);
        return IPC_ERR_OBJECT_DESTROYED;
    }

    if (!ipc_signal_consume_locked(signal)) {
        portEXIT_CRITICAL(&signal->header.lock);
        return IPC_ERR_NOT_READY;
    }

    signal->stats.waits++;
    ipc_signal_update_ready_locked(signal);
    portEXIT_CRITICAL(&signal->header.lock);
    return IPC_OK;
}

static ipc_error_t ipc_signal_wait_internal(ipc_handle_t handle,
                                            uint64_t timeout_us)
{
    ipc_signal_t *signal = NULL;
    ipc_error_t err = ipc_signal_validate(handle, &signal);
    if (err != IPC_OK) {
        return err;
    }

    portENTER_CRITICAL(&signal->header.lock);
    if (signal->header.destroyed) {
        portEXIT_CRITICAL(&signal->header.lock);
        return IPC_ERR_OBJECT_DESTROYED;
    }

    if (ipc_signal_consume_locked(signal)) {
        signal->stats.waits++;
        ipc_signal_update_ready_locked(signal);
        portEXIT_CRITICAL(&signal->header.lock);
        return IPC_OK;
    }

    ipc_waiter_t waiter;
    ipc_waiter_prepare(&waiter);
    ipc_waiter_enqueue(&signal->waiters, &waiter);
    ipc_signal_after_enqueue(signal);
    portEXIT_CRITICAL(&signal->header.lock);

    ipc_wait_result_t wait_result;
    if (timeout_us == 0) {
        wait_result = IPC_WAIT_RESULT_TIMEOUT;
    } else if (timeout_us == M_TIMER_TIMEOUT_FOREVER) {
        wait_result = ipc_waiter_block(&waiter, NULL);
    } else {
        wait_result = ipc_waiter_timed_block(&waiter, timeout_us);
    }

    portENTER_CRITICAL(&signal->header.lock);
    bool removed = ipc_waiter_remove(&signal->waiters, &waiter);
    if (removed) {
        ipc_signal_after_dequeue(signal);
    }

    ipc_error_t result = IPC_ERR_SHUTDOWN;
    switch (wait_result) {
    case IPC_WAIT_RESULT_OK:
        if (signal->header.destroyed) {
            result = IPC_ERR_OBJECT_DESTROYED;
            break;
        }
        if (ipc_signal_consume_locked(signal)) {
            signal->stats.waits++;
            ipc_signal_update_ready_locked(signal);
            result = IPC_OK;
        } else {
            result = IPC_ERR_SHUTDOWN;
        }
        break;
    case IPC_WAIT_RESULT_TIMEOUT:
        signal->stats.timeouts++;
        ipc_signal_update_ready_locked(signal);
        result = IPC_ERR_TIMEOUT;
        break;
    case IPC_WAIT_RESULT_OBJECT_DESTROYED:
        result = IPC_ERR_OBJECT_DESTROYED;
        break;
    default:
        result = IPC_ERR_SHUTDOWN;
        break;
    }

    portEXIT_CRITICAL(&signal->header.lock);
    return result;
}

ipc_error_t ipc_signal_wait(ipc_handle_t handle)
{
    return ipc_signal_wait_internal(handle, M_TIMER_TIMEOUT_FOREVER);
}

ipc_error_t ipc_signal_timed_wait(ipc_handle_t handle, uint64_t timeout_us)
{
    return ipc_signal_wait_internal(handle, timeout_us);
}

ipc_error_t ipc_signal_waitset_subscribe(ipc_handle_t handle,
                                         ipc_waitset_listener_t *listener,
                                         ipc_waitset_ready_cb_t callback,
                                         void *user_data)
{
    if (listener == NULL || callback == NULL) {
        return IPC_ERR_INVALID_ARGUMENT;
    }

    ipc_signal_t *signal = NULL;
    ipc_error_t err = ipc_signal_validate(handle, &signal);
    if (err != IPC_OK) {
        return err;
    }

    portENTER_CRITICAL(&signal->header.lock);
    listener->callback = callback;
    listener->user_data = user_data;
    listener->next = signal->listeners;
    signal->listeners = listener;
    bool ready = ipc_signal_is_ready(signal);
    portEXIT_CRITICAL(&signal->header.lock);

    callback(handle, ready, user_data);
    return IPC_OK;
}

ipc_error_t ipc_signal_waitset_unsubscribe(ipc_handle_t handle,
                                           ipc_waitset_listener_t *listener)
{
    if (listener == NULL) {
        return IPC_ERR_INVALID_ARGUMENT;
    }

    ipc_signal_t *signal = NULL;
    ipc_error_t err = ipc_signal_validate(handle, &signal);
    if (err != IPC_OK) {
        return err;
    }

    portENTER_CRITICAL(&signal->header.lock);
    ipc_waitset_listener_t **current = &signal->listeners;
    while (*current != NULL) {
        if (*current == listener) {
            *current = listener->next;
            listener->next = NULL;
            portEXIT_CRITICAL(&signal->header.lock);
            return IPC_OK;
        }
        current = &(*current)->next;
    }
    portEXIT_CRITICAL(&signal->header.lock);
    return IPC_ERR_INVALID_ARGUMENT;
}
