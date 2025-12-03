/**
 * @file        main/kernel/core/ipc/ipc_event_flags.c
 * @brief       Implements the event flags primitive for the IPC subsystem.
 * @details     Maintains masks, wait queues, and waitset notifications while coordinating Magnolia scheduler semantics.
 */

#include <stddef.h>
#include <string.h>

#include "kernel/core/ipc/ipc_event_flags_private.h"

static ipc_event_flags_t g_event_flags[IPC_MAX_EVENT_FLAGS];

/**
 * @brief Access the registry that tracks event flags handles.
 */
static inline ipc_handle_registry_t *ipc_event_flags_registry(void)
{
    return ipc_core_event_flags_registry();
}

ipc_event_flags_t *ipc_event_flags_lookup(ipc_handle_t handle)
{
    ipc_object_type_t type;
    uint16_t index;
    uint16_t generation;

    if (!ipc_handle_unpack(handle, &type, &index, &generation)) {
        return NULL;
    }

    if (type != IPC_OBJECT_EVENT_FLAGS || index >= IPC_MAX_EVENT_FLAGS) {
        return NULL;
    }

    ipc_handle_registry_t *registry = ipc_event_flags_registry();
    if (registry->generation[index] != generation) {
        return NULL;
    }

    return &g_event_flags[index];
}

void ipc_event_flags_module_init(void)
{
    memset(g_event_flags, 0, sizeof(g_event_flags));
    for (size_t i = 0; i < IPC_MAX_EVENT_FLAGS; i++) {
        g_event_flags[i].header.lock = (portMUX_TYPE)portMUX_INITIALIZER_UNLOCKED;
    }
}

/**
 * @brief Check whether the event flags mask currently indicates ready.
 */
static bool ipc_event_flags_is_ready(const ipc_event_flags_t *event_flags)
{
    return (event_flags != NULL && event_flags->mask != 0);
}

/**
 * @brief Notify every waitset listener about a readiness transition.
 */
static void ipc_event_flags_notify_waitsets(ipc_event_flags_t *event_flags,
                                            bool ready)
{
    ipc_waitset_listener_t *iter = event_flags->listeners;

    while (iter != NULL) {
        ipc_waitset_listener_t *next = iter->next;
        ipc_waitset_ready_cb_t callback = iter->callback;
        void *user = iter->user_data;
        ipc_handle_t handle = event_flags->header.handle;

        portEXIT_CRITICAL(&event_flags->header.lock);
        if (callback != NULL) {
            callback(handle, ready, user);
        }
        portENTER_CRITICAL(&event_flags->header.lock);
        iter = next;
    }
}

/**
 * @brief Update the ready/not ready flag while holding the lock.
 */
static void ipc_event_flags_update_ready_locked(ipc_event_flags_t *event_flags)
{
    bool ready = ipc_event_flags_is_ready(event_flags);
    if (ready == event_flags->ready_state) {
        return;
    }

    event_flags->ready_state = ready;
    ipc_event_flags_notify_waitsets(event_flags, ready);
}

/**
 * @brief Evaluate whether the posted mask matches the wait type while holding the lock.
 */
static bool ipc_event_flags_mask_satisfied(const ipc_event_flags_t *event_flags,
                                           ipc_event_flags_wait_type_t wait_type,
                                           uint32_t mask,
                                           uint32_t *out_matched)
{
    if (event_flags == NULL || mask == 0 || out_matched == NULL) {
        return false;
    }

    uint32_t current = event_flags->mask;
    switch (wait_type) {
    case IPC_EVENT_FLAGS_WAIT_ANY: {
        uint32_t matched = current & mask;
        if (matched != 0) {
            *out_matched = matched;
            return true;
        }
        break;
    }
    case IPC_EVENT_FLAGS_WAIT_ALL:
        if ((current & mask) == mask) {
            *out_matched = mask;
            return true;
        }
        break;
    case IPC_EVENT_FLAGS_WAIT_MASK:
        if (event_flags->mask_mode == IPC_EVENT_FLAGS_MASK_MODE_SUPERSET) {
            if ((current & mask) == mask) {
                *out_matched = mask;
                return true;
            }
        } else {
            if (current == mask) {
                *out_matched = mask;
                return true;
            }
        }
        break;
    default:
        break;
    }

    return false;
}

/**
 * @brief Apply auto-clear semantics to the consumed bits when required.
 */
static void ipc_event_flags_apply_auto_clear(ipc_event_flags_t *event_flags,
                                             uint32_t consumed)
{
    if (consumed == 0) {
        return;
    }

    if (event_flags->mode == IPC_EVENT_FLAGS_MODE_AUTO_CLEAR) {
        event_flags->mask &= ~consumed;
    }
}

/**
 * @brief Increment the enqueue count after adding a waiter.
 */
static void ipc_event_flags_after_enqueue(ipc_event_flags_t *event_flags)
{
    event_flags->header.waiting_tasks++;
}

/**
 * @brief Decrement the waiting count after removing a waiter.
 */
static void ipc_event_flags_after_dequeue(ipc_event_flags_t *event_flags)
{
    if (event_flags->header.waiting_tasks > 0) {
        event_flags->header.waiting_tasks--;
    }
}

/**
 * @brief Retrieve the event flags waiter container from the wait queue node.
 */
static inline ipc_event_flags_waiter_t *
ipc_event_flags_waiter_from_queue(ipc_waiter_t *waiter)
{
    return (ipc_event_flags_waiter_t *)((char *)waiter
                                        - offsetof(ipc_event_flags_waiter_t, wait));
}

/**
 * @brief Wake waiters whose predicates are satisfied while locked.
 */
static void ipc_event_flags_service_waiters_locked(ipc_event_flags_t *event_flags)
{
    if (event_flags->waiters.count == 0) {
        return;
    }

    ipc_waiter_t *current = event_flags->waiters.head;
    while (current != NULL) {
        ipc_waiter_t *next = current->next;
        ipc_event_flags_waiter_t *waiter =
                ipc_event_flags_waiter_from_queue(current);
        uint32_t matched = 0;

        if (!ipc_event_flags_mask_satisfied(event_flags,
                                            waiter->wait_type,
                                            waiter->mask,
                                            &matched)) {
            current = next;
            continue;
        }

        bool removed = ipc_waiter_remove(&event_flags->waiters, current);
        if (removed) {
            ipc_event_flags_after_dequeue(event_flags);
            ipc_event_flags_apply_auto_clear(event_flags, matched);
            m_sched_wait_wake(&current->ctx, M_SCHED_WAIT_RESULT_OK);
        }

        current = next;
    }
}

/**
 * @brief Validate a handle and return the corresponding event flags object.
 */
static ipc_error_t ipc_event_flags_validate(ipc_handle_t handle,
                                            ipc_event_flags_t **out_flags)
{
    ipc_event_flags_t *event_flags = ipc_event_flags_lookup(handle);
    if (event_flags == NULL) {
        return IPC_ERR_INVALID_HANDLE;
    }

    if (out_flags != NULL) {
        *out_flags = event_flags;
    }

    return IPC_OK;
}

/**
 * @brief Check that a wait type enum value is supported.
 */
static bool ipc_event_flags_wait_type_valid(ipc_event_flags_wait_type_t wait_type)
{
    return (wait_type == IPC_EVENT_FLAGS_WAIT_ANY
            || wait_type == IPC_EVENT_FLAGS_WAIT_ALL
            || wait_type == IPC_EVENT_FLAGS_WAIT_MASK);
}

ipc_error_t ipc_event_flags_create(ipc_event_flags_mode_t mode,
                                   ipc_event_flags_mask_mode_t mask_mode,
                                   ipc_handle_t *out_handle)
{
    if (out_handle == NULL) {
        return IPC_ERR_INVALID_ARGUMENT;
    }

    if (mode != IPC_EVENT_FLAGS_MODE_AUTO_CLEAR
        && mode != IPC_EVENT_FLAGS_MODE_MANUAL_CLEAR) {
        return IPC_ERR_INVALID_ARGUMENT;
    }

    if (mask_mode != IPC_EVENT_FLAGS_MASK_MODE_EXACT
        && mask_mode != IPC_EVENT_FLAGS_MASK_MODE_SUPERSET) {
        return IPC_ERR_INVALID_ARGUMENT;
    }

    ipc_handle_registry_t *registry = ipc_event_flags_registry();
    if (registry == NULL) {
        return IPC_ERR_INVALID_ARGUMENT;
    }

    uint16_t index = 0;
    ipc_handle_t handle = IPC_HANDLE_INVALID;
    ipc_error_t alloc = ipc_handle_allocate(registry, &index, &handle);
    if (alloc != IPC_OK) {
        return alloc;
    }

    ipc_event_flags_t *event_flags = &g_event_flags[index];
    memset(event_flags, 0, sizeof(*event_flags));
    event_flags->header.lock = (portMUX_TYPE)portMUX_INITIALIZER_UNLOCKED;
    event_flags->header.handle = handle;
    event_flags->header.type = IPC_OBJECT_EVENT_FLAGS;
    event_flags->header.generation = registry->generation[index];
    event_flags->mode = mode;
    event_flags->mask_mode = mask_mode;
    event_flags->ready_state = false;
    ipc_wait_queue_init(&event_flags->waiters);

    *out_handle = handle;
    return IPC_OK;
}

ipc_error_t ipc_event_flags_destroy(ipc_handle_t handle)
{
    ipc_event_flags_t *event_flags = NULL;
    ipc_error_t err = ipc_event_flags_validate(handle, &event_flags);
    if (err != IPC_OK) {
        return err;
    }

    portENTER_CRITICAL(&event_flags->header.lock);
    if (event_flags->header.destroyed) {
        portEXIT_CRITICAL(&event_flags->header.lock);
        return IPC_ERR_OBJECT_DESTROYED;
    }

    event_flags->header.destroyed = true;
    event_flags->mask = 0;
    event_flags->ready_state = false;
    ipc_wake_all(&event_flags->waiters, IPC_WAIT_RESULT_OBJECT_DESTROYED);
    event_flags->header.waiting_tasks = 0;
    ipc_event_flags_notify_waitsets(event_flags, false);
    ipc_wait_queue_init(&event_flags->waiters);
    event_flags->listeners = NULL;
    portEXIT_CRITICAL(&event_flags->header.lock);

    ipc_handle_registry_t *registry = ipc_event_flags_registry();
    uint16_t index = (uint16_t)(handle & IPC_HANDLE_INDEX_MASK);
    ipc_handle_release(registry, index);
    return IPC_OK;
}

ipc_error_t ipc_event_flags_set(ipc_handle_t handle, uint32_t bits)
{
    ipc_event_flags_t *event_flags = NULL;
    ipc_error_t err = ipc_event_flags_validate(handle, &event_flags);
    if (err != IPC_OK) {
        return err;
    }

    if (bits == 0) {
        return IPC_OK;
    }

    portENTER_CRITICAL(&event_flags->header.lock);
    if (event_flags->header.destroyed) {
        portEXIT_CRITICAL(&event_flags->header.lock);
        return IPC_ERR_OBJECT_DESTROYED;
    }

    event_flags->mask |= bits;
    event_flags->stats.sets++;
    ipc_event_flags_update_ready_locked(event_flags);
    ipc_event_flags_service_waiters_locked(event_flags);
    ipc_event_flags_update_ready_locked(event_flags);
    portEXIT_CRITICAL(&event_flags->header.lock);
    return IPC_OK;
}

ipc_error_t ipc_event_flags_clear(ipc_handle_t handle, uint32_t bits)
{
    ipc_event_flags_t *event_flags = NULL;
    ipc_error_t err = ipc_event_flags_validate(handle, &event_flags);
    if (err != IPC_OK) {
        return err;
    }

    if (bits == 0) {
        return IPC_OK;
    }

    portENTER_CRITICAL(&event_flags->header.lock);
    if (event_flags->header.destroyed) {
        portEXIT_CRITICAL(&event_flags->header.lock);
        return IPC_ERR_OBJECT_DESTROYED;
    }

    event_flags->mask &= ~bits;
    event_flags->stats.clears++;
    ipc_event_flags_update_ready_locked(event_flags);
    portEXIT_CRITICAL(&event_flags->header.lock);
    return IPC_OK;
}

ipc_error_t ipc_event_flags_read(ipc_handle_t handle, uint32_t *out_mask)
{
    if (out_mask == NULL) {
        return IPC_ERR_INVALID_ARGUMENT;
    }

    ipc_event_flags_t *event_flags = NULL;
    ipc_error_t err = ipc_event_flags_validate(handle, &event_flags);
    if (err != IPC_OK) {
        return err;
    }

    portENTER_CRITICAL(&event_flags->header.lock);
    if (event_flags->header.destroyed) {
        portEXIT_CRITICAL(&event_flags->header.lock);
        return IPC_ERR_OBJECT_DESTROYED;
    }

    *out_mask = event_flags->mask;
    portEXIT_CRITICAL(&event_flags->header.lock);
    return IPC_OK;
}

/**
 * @brief Perform a wait on the requested predicate with an optional timeout.
 */
static ipc_error_t ipc_event_flags_wait_internal(ipc_handle_t handle,
                                                 ipc_event_flags_wait_type_t wait_type,
                                                 uint32_t mask,
                                                 uint64_t timeout_us)
{
    if (!ipc_event_flags_wait_type_valid(wait_type) || mask == 0) {
        return IPC_ERR_INVALID_ARGUMENT;
    }

    ipc_event_flags_t *event_flags = NULL;
    ipc_error_t err = ipc_event_flags_validate(handle, &event_flags);
    if (err != IPC_OK) {
        return err;
    }

    uint32_t matched = 0;
    portENTER_CRITICAL(&event_flags->header.lock);
    if (event_flags->header.destroyed) {
        portEXIT_CRITICAL(&event_flags->header.lock);
        return IPC_ERR_OBJECT_DESTROYED;
    }

    if (ipc_event_flags_mask_satisfied(event_flags,
                                       wait_type,
                                       mask,
                                       &matched)) {
        ipc_event_flags_apply_auto_clear(event_flags, matched);
        event_flags->stats.waits++;
        ipc_event_flags_update_ready_locked(event_flags);
        portEXIT_CRITICAL(&event_flags->header.lock);
        return IPC_OK;
    }

    ipc_event_flags_waiter_t waiter = {0};
    waiter.wait_type = wait_type;
    waiter.mask = mask;
    ipc_waiter_prepare(&waiter.wait, M_SCHED_WAIT_REASON_EVENT_FLAGS);
    ipc_waiter_enqueue(&event_flags->waiters, &waiter.wait);
    ipc_event_flags_after_enqueue(event_flags);
    portEXIT_CRITICAL(&event_flags->header.lock);

    ipc_wait_result_t wait_result;
    if (timeout_us == 0) {
        wait_result = IPC_WAIT_RESULT_TIMEOUT;
    } else if (timeout_us == M_TIMER_TIMEOUT_FOREVER) {
        wait_result = ipc_waiter_block(&waiter.wait, NULL);
    } else {
        wait_result = ipc_waiter_timed_block(&waiter.wait, timeout_us);
    }

    portENTER_CRITICAL(&event_flags->header.lock);
    bool removed = ipc_waiter_remove(&event_flags->waiters, &waiter.wait);
    if (removed) {
        ipc_event_flags_after_dequeue(event_flags);
    }

    ipc_error_t result = IPC_ERR_SHUTDOWN;
    switch (wait_result) {
    case IPC_WAIT_RESULT_OK:
        if (event_flags->header.destroyed) {
            result = IPC_ERR_OBJECT_DESTROYED;
            break;
        }
        event_flags->stats.waits++;
        ipc_event_flags_update_ready_locked(event_flags);
        result = IPC_OK;
        break;
    case IPC_WAIT_RESULT_TIMEOUT:
        event_flags->stats.timeouts++;
        ipc_event_flags_update_ready_locked(event_flags);
        result = IPC_ERR_TIMEOUT;
        break;
    case IPC_WAIT_RESULT_OBJECT_DESTROYED:
        result = IPC_ERR_OBJECT_DESTROYED;
        break;
    default:
        result = IPC_ERR_SHUTDOWN;
        break;
    }

    portEXIT_CRITICAL(&event_flags->header.lock);
    return result;
}

ipc_error_t ipc_event_flags_try_wait(ipc_handle_t handle,
                                     ipc_event_flags_wait_type_t wait_type,
                                     uint32_t mask)
{
    if (!ipc_event_flags_wait_type_valid(wait_type) || mask == 0) {
        return IPC_ERR_INVALID_ARGUMENT;
    }

    ipc_event_flags_t *event_flags = NULL;
    ipc_error_t err = ipc_event_flags_validate(handle, &event_flags);
    if (err != IPC_OK) {
        return err;
    }

    uint32_t matched = 0;
    portENTER_CRITICAL(&event_flags->header.lock);
    if (event_flags->header.destroyed) {
        portEXIT_CRITICAL(&event_flags->header.lock);
        return IPC_ERR_OBJECT_DESTROYED;
    }

    if (!ipc_event_flags_mask_satisfied(event_flags,
                                        wait_type,
                                        mask,
                                        &matched)) {
        portEXIT_CRITICAL(&event_flags->header.lock);
        return IPC_ERR_NOT_READY;
    }

    ipc_event_flags_apply_auto_clear(event_flags, matched);
    event_flags->stats.waits++;
    ipc_event_flags_update_ready_locked(event_flags);
    portEXIT_CRITICAL(&event_flags->header.lock);
    return IPC_OK;
}

ipc_error_t ipc_event_flags_wait(ipc_handle_t handle,
                                 ipc_event_flags_wait_type_t wait_type,
                                 uint32_t mask)
{
    return ipc_event_flags_wait_internal(handle, wait_type, mask,
                                         M_TIMER_TIMEOUT_FOREVER);
}

ipc_error_t ipc_event_flags_timed_wait(ipc_handle_t handle,
                                       ipc_event_flags_wait_type_t wait_type,
                                       uint32_t mask,
                                       uint64_t timeout_us)
{
    return ipc_event_flags_wait_internal(handle, wait_type, mask, timeout_us);
}

ipc_error_t ipc_event_flags_waitset_subscribe(ipc_handle_t handle,
                                              ipc_waitset_listener_t *listener,
                                              ipc_waitset_ready_cb_t callback,
                                              void *user_data)
{
    if (listener == NULL || callback == NULL) {
        return IPC_ERR_INVALID_ARGUMENT;
    }

    ipc_event_flags_t *event_flags = NULL;
    ipc_error_t err = ipc_event_flags_validate(handle, &event_flags);
    if (err != IPC_OK) {
        return err;
    }

    portENTER_CRITICAL(&event_flags->header.lock);
    listener->callback = callback;
    listener->user_data = user_data;
    listener->next = event_flags->listeners;
    event_flags->listeners = listener;
    bool ready = ipc_event_flags_is_ready(event_flags);
    portEXIT_CRITICAL(&event_flags->header.lock);

    callback(handle, ready, user_data);

    return IPC_OK;
}

ipc_error_t ipc_event_flags_waitset_unsubscribe(ipc_handle_t handle,
                                                ipc_waitset_listener_t *listener)
{
    if (listener == NULL) {
        return IPC_ERR_INVALID_ARGUMENT;
    }

    ipc_event_flags_t *event_flags = NULL;
    ipc_error_t err = ipc_event_flags_validate(handle, &event_flags);
    if (err != IPC_OK) {
        return err;
    }

    portENTER_CRITICAL(&event_flags->header.lock);
    ipc_waitset_listener_t **current = &event_flags->listeners;
    while (*current != NULL) {
        if (*current == listener) {
            *current = listener->next;
            break;
        }
        current = &(*current)->next;
    }
    portEXIT_CRITICAL(&event_flags->header.lock);
    return IPC_OK;
}
