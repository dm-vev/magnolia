/**
 * @file        ipc_shm.c
 * @brief       Shared memory implementation for Magnolia IPC.
 * @details     Manages region allocation, attachment validation, wait queues,
 *              and raw/ring/packet I/O semantics.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "freertos/FreeRTOS.h"

#include "kernel/core/ipc/ipc_core.h"
#include "kernel/core/ipc/ipc_scheduler_bridge.h"
#include "kernel/core/ipc/ipc_shm_private.h"

#if CONFIG_MAGNOLIA_IPC_SHM_ENABLED

static ipc_shm_region_t g_shm_regions[IPC_MAX_SHM_REGIONS];
static const ipc_shm_region_options_t g_ipc_shm_default_options = {
    .ring_policy = IPC_SHM_RING_OVERWRITE_BLOCK,
    .packet_max_payload = CONFIG_MAGNOLIA_IPC_SHM_DEFAULT_PACKET_PAYLOAD,
};

void ipc_shm_module_init(void)
{
    memset(g_shm_regions, 0, sizeof(g_shm_regions));
    for (size_t i = 0; i < IPC_MAX_SHM_REGIONS; i++) {
        g_shm_regions[i].header.lock = (portMUX_TYPE)portMUX_INITIALIZER_UNLOCKED;
    }
}

#else

void ipc_shm_module_init(void)
{
}

static inline ipc_error_t ipc_shm_not_supported(void)
{
    return IPC_ERR_NOT_SUPPORTED;
}

ipc_error_t ipc_shm_create(size_t size,
                           ipc_shm_mode_t mode,
                           const ipc_shm_region_options_t *options,
                           ipc_handle_t *out_handle)
{
    (void)size;
    (void)mode;
    (void)options;
    (void)out_handle;
    return ipc_shm_not_supported();
}

ipc_error_t ipc_shm_destroy(ipc_handle_t handle)
{
    (void)handle;
    return ipc_shm_not_supported();
}

ipc_error_t ipc_shm_attach(ipc_handle_t handle,
                          ipc_shm_access_mode_t access,
                          const ipc_shm_attachment_options_t *options,
                          ipc_shm_attachment_t *out_attachment)
{
    (void)handle;
    (void)access;
    (void)options;
    (void)out_attachment;
    return ipc_shm_not_supported();
}

ipc_error_t ipc_shm_detach(ipc_shm_attachment_t *attachment)
{
    (void)attachment;
    return ipc_shm_not_supported();
}

ipc_error_t ipc_shm_read(ipc_shm_attachment_t *attachment,
                        void *dest,
                        size_t length,
                        uint64_t timeout_us)
{
    (void)attachment;
    (void)dest;
    (void)length;
    (void)timeout_us;
    return ipc_shm_not_supported();
}

ipc_error_t ipc_shm_write(ipc_shm_attachment_t *attachment,
                         const void *data,
                         size_t length,
                         uint64_t timeout_us)
{
    (void)attachment;
    (void)data;
    (void)length;
    (void)timeout_us;
    return ipc_shm_not_supported();
}

ipc_error_t ipc_shm_read_timed(ipc_shm_attachment_t *attachment,
                               void *dest,
                               size_t length,
                               uint64_t timeout_us)
{
    (void)attachment;
    (void)dest;
    (void)length;
    (void)timeout_us;
    return ipc_shm_not_supported();
}

ipc_error_t ipc_shm_write_timed(ipc_shm_attachment_t *attachment,
                                const void *data,
                                size_t length,
                                uint64_t timeout_us)
{
    (void)attachment;
    (void)data;
    (void)length;
    (void)timeout_us;
    return ipc_shm_not_supported();
}

ipc_error_t ipc_shm_try_read(ipc_shm_attachment_t *attachment,
                            void *dest,
                            size_t length)
{
    (void)attachment;
    (void)dest;
    (void)length;
    return ipc_shm_not_supported();
}

ipc_error_t ipc_shm_try_write(ipc_shm_attachment_t *attachment,
                             const void *data,
                             size_t length)
{
    (void)attachment;
    (void)data;
    (void)length;
    return ipc_shm_not_supported();
}

ipc_error_t ipc_shm_control(ipc_handle_t handle,
                            ipc_shm_control_command_t cmd,
                            void *arg)
{
    (void)handle;
    (void)cmd;
    (void)arg;
    return IPC_ERR_NOT_SUPPORTED;
}

ipc_error_t ipc_shm_query(ipc_handle_t handle, ipc_shm_info_t *info)
{
    (void)handle;
    (void)info;
    return ipc_shm_not_supported();
}

#endif
ipc_shm_region_t *ipc_shm_lookup(ipc_handle_t handle)
{
    ipc_object_type_t type;
    uint16_t index;
    uint16_t generation;

    if (!ipc_handle_unpack(handle, &type, &index, &generation)) {
        return NULL;
    }

    if (type != IPC_OBJECT_SHM_REGION || index >= IPC_MAX_SHM_REGIONS) {
        return NULL;
    }

    ipc_handle_registry_t *registry = ipc_core_shm_registry();
    if (registry->generation[index] != generation) {
        return NULL;
    }

    return &g_shm_regions[index];
}

static inline uint8_t *ipc_shm_memory_ptr(const ipc_shm_region_t *region);
/**
 * @brief   Copy data into the circular region buffer.
 */
static void ipc_shm_memcpy_to_region(ipc_shm_region_t *region,
                                     size_t offset,
                                     const void *src,
                                     size_t length);
/**
 * @brief   Copy data out of the circular region buffer.
 */
static void ipc_shm_memcpy_from_region(ipc_shm_region_t *region,
                                       size_t offset,
                                       void *dest,
                                       size_t length);
static void ipc_shm_reset_state(ipc_shm_region_t *region);
static void ipc_shm_clear_contents(ipc_shm_region_t *region);
static void ipc_shm_after_enqueue(ipc_shm_region_t *region);
static void ipc_shm_after_dequeue(ipc_shm_region_t *region);
/**
 * @brief   Translate scheduler wait results into IPC errors.
 */
static ipc_error_t ipc_shm_convert_wait_result(ipc_shm_region_t *region,
                                               ipc_wait_result_t wait_result,
                                               bool read);
/**
 * @brief   Ensure the attachment descriptor and handle remain valid.
 */
static ipc_error_t ipc_shm_attachment_validate(ipc_shm_attachment_t *attachment,
                                               ipc_shm_region_t **out_region);
static bool ipc_shm_cleanup_locked(ipc_shm_region_t *region,
                                   ipc_handle_t *out_handle);

/**
 * @brief   Configure a region descriptor for the requested shared memory mode.
 */
static ipc_error_t ipc_shm_configure_region(ipc_shm_region_t *region,
                                            size_t size,
                                            ipc_shm_mode_t mode,
                                            const ipc_shm_region_options_t *options)
{
    if (region == NULL || size == 0) {
        return IPC_ERR_INVALID_ARGUMENT;
    }

    ipc_shm_region_options_t opts = g_ipc_shm_default_options;
    if (options != NULL) {
        opts.ring_policy = options->ring_policy;
        opts.packet_max_payload = options->packet_max_payload;
    }

    region->mode = mode;
    region->region_size = size;
    region->ring_policy = opts.ring_policy;
    region->packet_max_payload = opts.packet_max_payload;
    region->raw_ready = true;
    ipc_wait_queue_init(&region->read_waiters);
    ipc_wait_queue_init(&region->write_waiters);
    ipc_shm_reset_state(region);
    return IPC_OK;
}

ipc_error_t ipc_shm_create(size_t size,
                           ipc_shm_mode_t mode,
                           const ipc_shm_region_options_t *options,
                           ipc_handle_t *out_handle)
{
    if (size == 0) {
        size = CONFIG_MAGNOLIA_IPC_SHM_DEFAULT_REGION_SIZE;
    }

    if (size == 0 || out_handle == NULL) {
        return IPC_ERR_INVALID_ARGUMENT;
    }

    if (mode == IPC_SHM_MODE_RING_BUFFER && size <= 1) {
        return IPC_ERR_INVALID_ARGUMENT;
    }

    if (mode == IPC_SHM_MODE_PACKET_BUFFER
        && size <= sizeof(ipc_shm_packet_header_t)) {
        return IPC_ERR_INVALID_ARGUMENT;
    }

#if !CONFIG_MAGNOLIA_IPC_SHM_ALLOW_RING_BUFFER
    if (mode == IPC_SHM_MODE_RING_BUFFER) {
        return IPC_ERR_NOT_SUPPORTED;
    }
#endif

#if !CONFIG_MAGNOLIA_IPC_SHM_ALLOW_PACKET_BUFFER
    if (mode == IPC_SHM_MODE_PACKET_BUFFER) {
        return IPC_ERR_NOT_SUPPORTED;
    }
#endif

    ipc_handle_registry_t *registry = ipc_core_shm_registry();
    uint16_t index = 0;
    ipc_handle_t handle = IPC_HANDLE_INVALID;
    ipc_error_t err = ipc_handle_allocate(registry, &index, &handle);
    if (err != IPC_OK) {
        return err;
    }

    ipc_shm_region_t *region = &g_shm_regions[index];
    portENTER_CRITICAL(&region->header.lock);
    region->header.handle = handle;
    region->header.type = IPC_OBJECT_SHM_REGION;
    region->header.generation = registry->generation[index];
    region->header.destroyed = false;
    region->attachment_count = 0;
    err = ipc_shm_configure_region(region, size, mode, options);
    if (err != IPC_OK) {
        portEXIT_CRITICAL(&region->header.lock);
        ipc_handle_release(registry, index);
        return err;
    }

    if (mode == IPC_SHM_MODE_PACKET_BUFFER) {
        size_t available = size - sizeof(ipc_shm_packet_header_t);
        if (available == 0) {
            portEXIT_CRITICAL(&region->header.lock);
            ipc_handle_release(registry, index);
            return IPC_ERR_INVALID_ARGUMENT;
        }
        if (region->packet_max_payload == 0
            || region->packet_max_payload > available) {
            region->packet_max_payload = available;
        }
    }

    region->memory = pvPortMalloc(size);
    if (region->memory == NULL) {
        portEXIT_CRITICAL(&region->header.lock);
        ipc_handle_release(registry, index);
        return IPC_ERR_NO_SPACE;
    }

    memset(region->memory, 0, size);
    portEXIT_CRITICAL(&region->header.lock);
    *out_handle = handle;
    return IPC_OK;
}

ipc_error_t ipc_shm_destroy(ipc_handle_t handle)
{
    ipc_shm_region_t *region = ipc_shm_lookup(handle);
    if (region == NULL) {
        return IPC_ERR_INVALID_HANDLE;
    }

    ipc_handle_t release_handle = IPC_HANDLE_INVALID;
    portENTER_CRITICAL(&region->header.lock);
    if (region->header.destroyed) {
        portEXIT_CRITICAL(&region->header.lock);
        return IPC_ERR_OBJECT_DESTROYED;
    }

    region->header.destroyed = true;
    ipc_wake_all(&region->read_waiters, IPC_WAIT_RESULT_OBJECT_DESTROYED);
    ipc_wake_all(&region->write_waiters, IPC_WAIT_RESULT_OBJECT_DESTROYED);
    region->waiting_readers = 0;
    region->waiting_writers = 0;
    region->header.waiting_tasks = 0;

    bool needs_release = ipc_shm_cleanup_locked(region, &release_handle);
    portEXIT_CRITICAL(&region->header.lock);

    if (needs_release && release_handle != IPC_HANDLE_INVALID) {
        uint16_t index = (uint16_t)(release_handle & IPC_HANDLE_INDEX_MASK);
        ipc_handle_release(ipc_core_shm_registry(), index);
    }

    return IPC_OK;
}

/**
 * @brief   Check whether the access mode includes read permission.
 */
static bool ipc_shm_access_allows_read(ipc_shm_access_mode_t mode)
{
    return (mode == IPC_SHM_ACCESS_READ_ONLY
            || mode == IPC_SHM_ACCESS_READ_WRITE);
}

/**
 * @brief   Check whether the access mode includes write permission.
 */
static bool ipc_shm_access_allows_write(ipc_shm_access_mode_t mode)
{
    return (mode == IPC_SHM_ACCESS_WRITE_ONLY
            || mode == IPC_SHM_ACCESS_READ_WRITE);
}

ipc_error_t ipc_shm_attach(ipc_handle_t handle,
                          ipc_shm_access_mode_t access,
                          const ipc_shm_attachment_options_t *options,
                          ipc_shm_attachment_t *out_attachment)
{
    if (out_attachment == NULL) {
        return IPC_ERR_INVALID_ARGUMENT;
    }

    ipc_shm_region_t *region = ipc_shm_lookup(handle);
    if (region == NULL) {
        return IPC_ERR_INVALID_HANDLE;
    }

    if (access < IPC_SHM_ACCESS_READ_ONLY || access > IPC_SHM_ACCESS_READ_WRITE) {
        return IPC_ERR_INVALID_ARGUMENT;
    }

    portENTER_CRITICAL(&region->header.lock);
    if (region->header.destroyed) {
        portEXIT_CRITICAL(&region->header.lock);
        return IPC_ERR_OBJECT_DESTROYED;
    }

    region->attachment_count++;
    region->stats.attachments++;
    portEXIT_CRITICAL(&region->header.lock);

    size_t cursor = 0;
    if (options != NULL) {
        cursor = options->cursor_offset;
        if (cursor >= region->region_size) {
            cursor = 0;
        }
    }

    *out_attachment = (ipc_shm_attachment_t){
        .handle = handle,
        .mode = access,
        .cursor = cursor,
        .attached = true,
        .internal = region,
    };

    return IPC_OK;
}

ipc_error_t ipc_shm_detach(ipc_shm_attachment_t *attachment)
{
    if (attachment == NULL || !attachment->attached || attachment->internal == NULL) {
        return IPC_ERR_NOT_ATTACHED;
    }

    ipc_shm_region_t *region = (ipc_shm_region_t *)attachment->internal;
    if (region->header.handle != attachment->handle) {
        return IPC_ERR_INVALID_HANDLE;
    }

    ipc_handle_t release_handle = IPC_HANDLE_INVALID;
    portENTER_CRITICAL(&region->header.lock);
    if (region->attachment_count > 0) {
        region->attachment_count--;
    }

    bool needs_release = ipc_shm_cleanup_locked(region, &release_handle);
    portEXIT_CRITICAL(&region->header.lock);

    if (needs_release && release_handle != IPC_HANDLE_INVALID) {
        uint16_t index = (uint16_t)(release_handle & IPC_HANDLE_INDEX_MASK);
        ipc_handle_release(ipc_core_shm_registry(), index);
    }

    attachment->attached = false;
    attachment->internal = NULL;
    return IPC_OK;
}

/**
 * @brief   Compute the usable capacity for a ring buffer region.
 */
static size_t ipc_shm_ring_capacity(const ipc_shm_region_t *region)
{
    if (region == NULL || region->region_size == 0) {
        return 0;
    }

    return (region->region_size > 0) ? region->region_size - 1 : 0;
}

/**
 * @brief   Report the free space remaining in a ring region.
 */
static size_t ipc_shm_ring_free_space(const ipc_shm_region_t *region)
{
    size_t capacity = ipc_shm_ring_capacity(region);
    if (region == NULL || region->ring_used >= capacity) {
        return 0;
    }
    return capacity - region->ring_used;
}

/**
 * @brief   Determine how many bytes must be overwritten to fit length bytes.
 */
static size_t ipc_shm_ring_drop_amount(ipc_shm_region_t *region, size_t length)
{
    size_t free_space = ipc_shm_ring_free_space(region);
    if (length <= free_space) {
        return 0;
    }
    return length - free_space;
}

/**
 * @brief   Discard the oldest bytes from the ring buffer.
 */
static void ipc_shm_ring_drop_oldest(ipc_shm_region_t *region, size_t drop)
{
    if (drop == 0 || drop > region->ring_used) {
        drop = region->ring_used;
    }

    region->ring_head = (region->ring_head + drop) % region->region_size;
    region->ring_used -= drop;
    region->stats.ring_overflows += drop;
}

/**
 * @brief   Read data from a ring buffer region while holding the lock.
 */
static ipc_error_t ipc_shm_ring_read_common(ipc_shm_attachment_t *attachment,
                                            void *buffer,
                                            size_t buffer_size,
                                            size_t *out_transferred,
                                            uint64_t timeout_us,
                                            bool nonblocking,
                                            bool timed)
{
    if (attachment == NULL || buffer == NULL || buffer_size == 0) {
        return IPC_ERR_INVALID_ARGUMENT;
    }

    ipc_shm_region_t *region = (ipc_shm_region_t *)attachment->internal;
    if (region == NULL) {
        return IPC_ERR_INVALID_HANDLE;
    }

    bool use_deadline = (timed && timeout_us != M_TIMER_TIMEOUT_FOREVER);
    m_timer_deadline_t deadline = {0};
    if (use_deadline) {
        deadline = m_timer_deadline_from_relative(timeout_us);
    }

    portENTER_CRITICAL(&region->header.lock);
    while (true) {
        if (region->header.destroyed) {
            portEXIT_CRITICAL(&region->header.lock);
            return IPC_ERR_OBJECT_DESTROYED;
        }

        if (region->ring_used > 0) {
            size_t to_copy = buffer_size;
            if (to_copy > region->ring_used) {
                to_copy = region->ring_used;
            }

            ipc_shm_memcpy_from_region(region, region->ring_head, buffer, to_copy);
            region->ring_head = (region->ring_head + to_copy) % region->region_size;
            region->ring_used -= to_copy;
            region->stats.reads++;
            if (out_transferred != NULL) {
                *out_transferred = to_copy;
            }

            bool wake_writer = (region->waiting_writers > 0);
            portEXIT_CRITICAL(&region->header.lock);
            if (wake_writer) {
                ipc_wake_one(&region->write_waiters, IPC_WAIT_RESULT_OK);
            }
            return IPC_OK;
        }

        if (nonblocking) {
            portEXIT_CRITICAL(&region->header.lock);
            return IPC_ERR_EMPTY;
        }

        if (timed && timeout_us == 0) {
            portEXIT_CRITICAL(&region->header.lock);
            return IPC_ERR_TIMEOUT;
        }

        ipc_shm_waiter_t waiter_ctx = {0};
        ipc_waiter_prepare(&waiter_ctx.waiter, M_SCHED_WAIT_REASON_SHM_READ);
        waiter_ctx.requested = buffer_size;
        ipc_waiter_enqueue(&region->read_waiters, &waiter_ctx.waiter);
        region->waiting_readers++;
        ipc_shm_after_enqueue(region);
        portEXIT_CRITICAL(&region->header.lock);

        ipc_wait_result_t wait_result;
        if (use_deadline) {
            wait_result = ipc_waiter_block(&waiter_ctx.waiter, &deadline);
        } else {
            wait_result = ipc_waiter_block(&waiter_ctx.waiter, NULL);
        }

        portENTER_CRITICAL(&region->header.lock);
        bool removed = ipc_waiter_remove(&region->read_waiters,
                                          &waiter_ctx.waiter);
        if (removed) {
            region->waiting_readers--;
            ipc_shm_after_dequeue(region);
        }

        ipc_error_t converted =
                ipc_shm_convert_wait_result(region, wait_result, true);
        if (converted != IPC_OK) {
            portEXIT_CRITICAL(&region->header.lock);
            return converted;
        }

        // Loop to re-check readiness
    }
}

/**
 * @brief   Write data into the ring buffer region while holding the lock.
 */
static ipc_error_t ipc_shm_ring_write_common(ipc_shm_attachment_t *attachment,
                                             const void *data,
                                             size_t length,
                                             uint64_t timeout_us,
                                             bool nonblocking,
                                             bool timed)
{
    if (attachment == NULL || data == NULL || length == 0) {
        return IPC_ERR_INVALID_ARGUMENT;
    }

    ipc_shm_region_t *region = (ipc_shm_region_t *)attachment->internal;
    if (region == NULL) {
        return IPC_ERR_INVALID_HANDLE;
    }

    if (length > region->region_size) {
        return IPC_ERR_FULL;
    }

    bool use_deadline = (timed && timeout_us != M_TIMER_TIMEOUT_FOREVER);
    m_timer_deadline_t deadline = {0};
    if (use_deadline) {
        deadline = m_timer_deadline_from_relative(timeout_us);
    }

    portENTER_CRITICAL(&region->header.lock);
    while (true) {
        if (region->header.destroyed) {
            portEXIT_CRITICAL(&region->header.lock);
            return IPC_ERR_OBJECT_DESTROYED;
        }

        size_t free_space = ipc_shm_ring_free_space(region);
        if (free_space >= length) {
            ipc_shm_memcpy_to_region(region, region->ring_tail, data, length);
            region->ring_tail = (region->ring_tail + length) % region->region_size;
            region->ring_used += length;
            region->stats.writes++;
            bool wake_reader = (region->waiting_readers > 0);
            portEXIT_CRITICAL(&region->header.lock);
            if (wake_reader) {
                ipc_wake_one(&region->read_waiters, IPC_WAIT_RESULT_OK);
            }
            return IPC_OK;
        }

        if (region->ring_policy == IPC_SHM_RING_OVERWRITE_DROP_OLDEST) {
            size_t drop = ipc_shm_ring_drop_amount(region, length);
            ipc_shm_ring_drop_oldest(region, drop);
            continue;
        }

        if (nonblocking) {
            portEXIT_CRITICAL(&region->header.lock);
            return IPC_ERR_FULL;
        }

        if (timed && timeout_us == 0) {
            portEXIT_CRITICAL(&region->header.lock);
            return IPC_ERR_TIMEOUT;
        }

        ipc_shm_waiter_t waiter_ctx = {0};
        ipc_waiter_prepare(&waiter_ctx.waiter, M_SCHED_WAIT_REASON_SHM_WRITE);
        waiter_ctx.requested = length;
        ipc_waiter_enqueue(&region->write_waiters, &waiter_ctx.waiter);
        region->waiting_writers++;
        ipc_shm_after_enqueue(region);
        portEXIT_CRITICAL(&region->header.lock);

        ipc_wait_result_t wait_result;
        if (use_deadline) {
            wait_result = ipc_waiter_block(&waiter_ctx.waiter, &deadline);
        } else {
            wait_result = ipc_waiter_block(&waiter_ctx.waiter, NULL);
        }

        portENTER_CRITICAL(&region->header.lock);
        bool removed = ipc_waiter_remove(&region->write_waiters,
                                          &waiter_ctx.waiter);
        if (removed) {
            region->waiting_writers--;
            ipc_shm_after_dequeue(region);
        }

        ipc_error_t converted =
                ipc_shm_convert_wait_result(region, wait_result, false);
        if (converted != IPC_OK) {
            portEXIT_CRITICAL(&region->header.lock);
            return converted;
        }
    }
}

/**
 * @brief   Read a packet from the packet buffer mode region.
 */
static ipc_error_t ipc_shm_packet_read_common(ipc_shm_attachment_t *attachment,
                                              void *buffer,
                                              size_t buffer_size,
                                              size_t *out_transferred,
                                              uint64_t timeout_us,
                                              bool nonblocking,
                                              bool timed)
{
    if (attachment == NULL || buffer == NULL || buffer_size == 0) {
        return IPC_ERR_INVALID_ARGUMENT;
    }

    ipc_shm_region_t *region = (ipc_shm_region_t *)attachment->internal;
    if (region == NULL) {
        return IPC_ERR_INVALID_HANDLE;
    }

    bool use_deadline = (timed && timeout_us != M_TIMER_TIMEOUT_FOREVER);
    m_timer_deadline_t deadline = {0};
    if (use_deadline) {
        deadline = m_timer_deadline_from_relative(timeout_us);
    }

    portENTER_CRITICAL(&region->header.lock);
    while (true) {
        if (region->header.destroyed) {
            portEXIT_CRITICAL(&region->header.lock);
            return IPC_ERR_OBJECT_DESTROYED;
        }

        if (region->packet_count > 0) {
            ipc_shm_packet_header_t header = {0};
            ipc_shm_memcpy_from_region(region, region->packet_head, &header,
                                       sizeof(header));
            size_t payload = header.length;
            size_t total = sizeof(header) + payload;
            if (payload > buffer_size) {
                portEXIT_CRITICAL(&region->header.lock);
                return IPC_ERR_INVALID_ARGUMENT;
            }

            size_t payload_offset = (region->packet_head + sizeof(header))
                                    % region->region_size;
            ipc_shm_memcpy_from_region(region,
                                       payload_offset,
                                       buffer,
                                       payload);

            region->packet_head = (region->packet_head + total)
                                  % region->region_size;
            region->packet_bytes -= total;
            region->packet_count--;
            region->stats.reads++;
            if (out_transferred != NULL) {
                *out_transferred = payload;
            }

            bool wake_writer = (region->waiting_writers > 0);
            portEXIT_CRITICAL(&region->header.lock);
            if (wake_writer) {
                ipc_wake_one(&region->write_waiters, IPC_WAIT_RESULT_OK);
            }
            return IPC_OK;
        }

        if (nonblocking) {
            portEXIT_CRITICAL(&region->header.lock);
            return IPC_ERR_EMPTY;
        }

        if (timed && timeout_us == 0) {
            portEXIT_CRITICAL(&region->header.lock);
            return IPC_ERR_TIMEOUT;
        }

        ipc_shm_waiter_t waiter_ctx = {0};
        ipc_waiter_prepare(&waiter_ctx.waiter, M_SCHED_WAIT_REASON_SHM_READ);
        waiter_ctx.requested = buffer_size;
        ipc_waiter_enqueue(&region->read_waiters, &waiter_ctx.waiter);
        region->waiting_readers++;
        ipc_shm_after_enqueue(region);
        portEXIT_CRITICAL(&region->header.lock);

        ipc_wait_result_t wait_result;
        if (use_deadline) {
            wait_result = ipc_waiter_block(&waiter_ctx.waiter, &deadline);
        } else {
            wait_result = ipc_waiter_block(&waiter_ctx.waiter, NULL);
        }

        portENTER_CRITICAL(&region->header.lock);
        bool removed = ipc_waiter_remove(&region->read_waiters,
                                          &waiter_ctx.waiter);
        if (removed) {
            region->waiting_readers--;
            ipc_shm_after_dequeue(region);
        }

        ipc_error_t converted =
                ipc_shm_convert_wait_result(region, wait_result, true);
        if (converted != IPC_OK) {
            portEXIT_CRITICAL(&region->header.lock);
            return converted;
        }
    }
}

/**
 * @brief   Write a packet into the packet buffer mode region.
 */
static ipc_error_t ipc_shm_packet_write_common(ipc_shm_attachment_t *attachment,
                                               const void *data,
                                               size_t length,
                                               uint64_t timeout_us,
                                               bool nonblocking,
                                               bool timed)
{
    if (attachment == NULL || data == NULL || length == 0) {
        return IPC_ERR_INVALID_ARGUMENT;
    }

    ipc_shm_region_t *region = (ipc_shm_region_t *)attachment->internal;
    if (region == NULL) {
        return IPC_ERR_INVALID_HANDLE;
    }

    if (length > region->packet_max_payload) {
        return IPC_ERR_INVALID_ARGUMENT;
    }

    size_t total = sizeof(ipc_shm_packet_header_t) + length;
    if (total > region->region_size) {
        return IPC_ERR_FULL;
    }

    bool use_deadline = (timed && timeout_us != M_TIMER_TIMEOUT_FOREVER);
    m_timer_deadline_t deadline = {0};
    if (use_deadline) {
        deadline = m_timer_deadline_from_relative(timeout_us);
    }

    portENTER_CRITICAL(&region->header.lock);
    while (true) {
        if (region->header.destroyed) {
            portEXIT_CRITICAL(&region->header.lock);
            return IPC_ERR_OBJECT_DESTROYED;
        }

        size_t free_space = region->region_size - region->packet_bytes;
        if (free_space >= total) {
            ipc_shm_packet_header_t header = {
                .length = (uint16_t)length,
            };
            ipc_shm_memcpy_to_region(region,
                                     region->packet_tail,
                                     &header,
                                     sizeof(header));

            size_t payload_offset = (region->packet_tail + sizeof(header))
                                    % region->region_size;
            ipc_shm_memcpy_to_region(region,
                                     payload_offset,
                                     data,
                                     length);

            region->packet_tail = (region->packet_tail + total)
                                  % region->region_size;
            region->packet_bytes += total;
            region->packet_count++;
            region->stats.writes++;

            bool wake_reader = (region->waiting_readers > 0);
            portEXIT_CRITICAL(&region->header.lock);
            if (wake_reader) {
                ipc_wake_one(&region->read_waiters, IPC_WAIT_RESULT_OK);
            }
            return IPC_OK;
        }

        if (nonblocking) {
            portEXIT_CRITICAL(&region->header.lock);
            return IPC_ERR_FULL;
        }

        if (timed && timeout_us == 0) {
            portEXIT_CRITICAL(&region->header.lock);
            return IPC_ERR_TIMEOUT;
        }

        ipc_shm_waiter_t waiter_ctx = {0};
        ipc_waiter_prepare(&waiter_ctx.waiter, M_SCHED_WAIT_REASON_SHM_WRITE);
        waiter_ctx.requested = length;
        ipc_waiter_enqueue(&region->write_waiters, &waiter_ctx.waiter);
        region->waiting_writers++;
        ipc_shm_after_enqueue(region);
        portEXIT_CRITICAL(&region->header.lock);

        ipc_wait_result_t wait_result;
        if (use_deadline) {
            wait_result = ipc_waiter_block(&waiter_ctx.waiter, &deadline);
        } else {
            wait_result = ipc_waiter_block(&waiter_ctx.waiter, NULL);
        }

        portENTER_CRITICAL(&region->header.lock);
        bool removed = ipc_waiter_remove(&region->write_waiters,
                                          &waiter_ctx.waiter);
        if (removed) {
            region->waiting_writers--;
            ipc_shm_after_dequeue(region);
        }

        ipc_error_t converted =
                ipc_shm_convert_wait_result(region, wait_result, false);
        if (converted != IPC_OK) {
            portEXIT_CRITICAL(&region->header.lock);
            return converted;
        }
    }
}

/**
 * @brief   Perform a raw read using the attachment cursor.
 */
static ipc_error_t ipc_shm_raw_read(ipc_shm_attachment_t *attachment,
                                    void *buffer,
                                    size_t buffer_size,
                                    size_t *out_transferred)
{
    if (attachment == NULL || buffer == NULL || buffer_size == 0) {
        return IPC_ERR_INVALID_ARGUMENT;
    }

    ipc_shm_region_t *region = (ipc_shm_region_t *)attachment->internal;
    if (region == NULL) {
        return IPC_ERR_INVALID_HANDLE;
    }

    if (region->header.destroyed) {
        return IPC_ERR_OBJECT_DESTROYED;
    }

    if (attachment->cursor >= region->region_size) {
        return IPC_ERR_EMPTY;
    }

    size_t available = region->region_size - attachment->cursor;
    size_t to_copy = buffer_size < available ? buffer_size : available;
    memcpy(buffer,
           ipc_shm_memory_ptr(region) + attachment->cursor,
           to_copy);
    attachment->cursor += to_copy;
    region->stats.reads++;

    if (out_transferred != NULL) {
        *out_transferred = to_copy;
    }

    return to_copy > 0 ? IPC_OK : IPC_ERR_EMPTY;
}

/**
 * @brief   Perform a raw write using the attachment cursor.
 */
static ipc_error_t ipc_shm_raw_write(ipc_shm_attachment_t *attachment,
                                     const void *data,
                                     size_t length)
{
    if (attachment == NULL || data == NULL || length == 0) {
        return IPC_ERR_INVALID_ARGUMENT;
    }

    ipc_shm_region_t *region = (ipc_shm_region_t *)attachment->internal;
    if (region == NULL) {
        return IPC_ERR_INVALID_HANDLE;
    }

    if (region->header.destroyed) {
        return IPC_ERR_OBJECT_DESTROYED;
    }

    if (attachment->cursor + length > region->region_size) {
        return IPC_ERR_FULL;
    }

    memcpy((uint8_t *)region->memory + attachment->cursor, data, length);
    attachment->cursor += length;
    region->stats.writes++;
    return IPC_OK;
}

/**
 * @brief   Route a read operation to the appropriate mode helper.
 */
static ipc_error_t ipc_shm_dispatch_read(ipc_shm_attachment_t *attachment,
                                         void *buffer,
                                         size_t buffer_size,
                                         size_t *out_transferred,
                                         uint64_t timeout_us,
                                         bool nonblocking,
                                         bool timed)
{
    ipc_shm_region_t *region = NULL;
    ipc_error_t err = ipc_shm_attachment_validate(attachment, &region);
    if (err != IPC_OK) {
        return err;
    }

    if (!ipc_shm_access_allows_read(attachment->mode)) {
        return IPC_ERR_NO_PERMISSION;
    }

    switch (region->mode) {
    case IPC_SHM_MODE_RAW:
        return ipc_shm_raw_read(attachment, buffer, buffer_size, out_transferred);
    case IPC_SHM_MODE_RING_BUFFER:
        return ipc_shm_ring_read_common(attachment,
                                        buffer,
                                        buffer_size,
                                        out_transferred,
                                        timeout_us,
                                        nonblocking,
                                        timed);
    case IPC_SHM_MODE_PACKET_BUFFER:
        return ipc_shm_packet_read_common(attachment,
                                          buffer,
                                          buffer_size,
                                          out_transferred,
                                          timeout_us,
                                          nonblocking,
                                          timed);
    default:
        return IPC_ERR_INVALID_ARGUMENT;
    }
}

/**
 * @brief   Route a write operation to the appropriate mode helper.
 */
static ipc_error_t ipc_shm_dispatch_write(ipc_shm_attachment_t *attachment,
                                          const void *data,
                                          size_t length,
                                          uint64_t timeout_us,
                                          bool nonblocking,
                                          bool timed)
{
    ipc_shm_region_t *region = NULL;
    ipc_error_t err = ipc_shm_attachment_validate(attachment, &region);
    if (err != IPC_OK) {
        return err;
    }

    if (!ipc_shm_access_allows_write(attachment->mode)) {
        return IPC_ERR_NO_PERMISSION;
    }

    switch (region->mode) {
    case IPC_SHM_MODE_RAW:
        return ipc_shm_raw_write(attachment, data, length);
    case IPC_SHM_MODE_RING_BUFFER:
        return ipc_shm_ring_write_common(attachment,
                                         data,
                                         length,
                                         timeout_us,
                                         nonblocking,
                                         timed);
    case IPC_SHM_MODE_PACKET_BUFFER:
        return ipc_shm_packet_write_common(attachment,
                                           data,
                                           length,
                                           timeout_us,
                                           nonblocking,
                                           timed);
    default:
        return IPC_ERR_INVALID_ARGUMENT;
    }
}

ipc_error_t ipc_shm_read(ipc_shm_attachment_t *attachment,
                         void *out_buffer,
                         size_t buffer_size,
                         size_t *out_transferred)
{
    return ipc_shm_dispatch_read(attachment,
                                 out_buffer,
                                 buffer_size,
                                 out_transferred,
                                 0,
                                 false,
                                 false);
}

ipc_error_t ipc_shm_read_timed(ipc_shm_attachment_t *attachment,
                               void *out_buffer,
                               size_t buffer_size,
                               size_t *out_transferred,
                               uint64_t timeout_us)
{
    if (timeout_us == 0) {
        return IPC_ERR_TIMEOUT;
    }

    bool timed = (timeout_us != M_TIMER_TIMEOUT_FOREVER);
    return ipc_shm_dispatch_read(attachment,
                                 out_buffer,
                                 buffer_size,
                                 out_transferred,
                                 timeout_us,
                                 false,
                                 timed);
}

ipc_error_t ipc_shm_try_read(ipc_shm_attachment_t *attachment,
                             void *out_buffer,
                             size_t buffer_size,
                             size_t *out_transferred)
{
    return ipc_shm_dispatch_read(attachment,
                                 out_buffer,
                                 buffer_size,
                                 out_transferred,
                                 0,
                                 true,
                                 false);
}

ipc_error_t ipc_shm_write(ipc_shm_attachment_t *attachment,
                          const void *data,
                          size_t length)
{
    return ipc_shm_dispatch_write(attachment,
                                  data,
                                  length,
                                  0,
                                  false,
                                  false);
}

ipc_error_t ipc_shm_write_timed(ipc_shm_attachment_t *attachment,
                                const void *data,
                                size_t length,
                                uint64_t timeout_us)
{
    if (timeout_us == 0) {
        return IPC_ERR_TIMEOUT;
    }

    bool timed = (timeout_us != M_TIMER_TIMEOUT_FOREVER);
    return ipc_shm_dispatch_write(attachment,
                                  data,
                                  length,
                                  timeout_us,
                                  false,
                                  timed);
}

ipc_error_t ipc_shm_try_write(ipc_shm_attachment_t *attachment,
                              const void *data,
                              size_t length)
{
    return ipc_shm_dispatch_write(attachment,
                                  data,
                                  length,
                                  0,
                                  true,
                                  false);
}

ipc_error_t ipc_shm_query(ipc_handle_t handle, ipc_shm_info_t *info)
{
    if (info == NULL) {
        return IPC_ERR_INVALID_ARGUMENT;
    }

    ipc_shm_region_t *region = ipc_shm_lookup(handle);
    if (region == NULL) {
        return IPC_ERR_INVALID_HANDLE;
    }

    portENTER_CRITICAL(&region->header.lock);
    info->region_size = region->region_size;
    info->mode = region->mode;
    info->attachment_count = region->attachment_count;
    info->waiting_readers = region->waiting_readers;
    info->waiting_writers = region->waiting_writers;
    info->destroyed = region->header.destroyed;
    info->ring_capacity = region->region_size;
    info->ring_used = region->ring_used;
    info->ring_overflows = region->stats.ring_overflows;
    info->packet_inflight = region->packet_count;
    info->packet_drops = region->stats.packet_drops;
    portEXIT_CRITICAL(&region->header.lock);

    return IPC_OK;
}

ipc_error_t ipc_shm_control(ipc_handle_t handle,
                            ipc_shm_control_command_t cmd,
                            void *arg)
{
    if (cmd == IPC_SHM_CONTROL_GET_INFO) {
        if (arg == NULL) {
            return IPC_ERR_INVALID_ARGUMENT;
        }
        return ipc_shm_query(handle, (ipc_shm_info_t *)arg);
    }

    ipc_shm_region_t *region = ipc_shm_lookup(handle);
    if (region == NULL) {
        return IPC_ERR_INVALID_HANDLE;
    }

    portENTER_CRITICAL(&region->header.lock);
    if (region->header.destroyed) {
        portEXIT_CRITICAL(&region->header.lock);
        return IPC_ERR_OBJECT_DESTROYED;
    }

    switch (cmd) {
    case IPC_SHM_CONTROL_FLUSH:
        ipc_shm_clear_contents(region);
        break;
    case IPC_SHM_CONTROL_RESET:
        ipc_shm_clear_contents(region);
        region->stats = (ipc_shm_stats_t){0};
        break;
    case IPC_SHM_CONTROL_NOTIFY_READERS:
        ipc_wake_all(&region->read_waiters, IPC_WAIT_RESULT_OK);
        if (region->header.waiting_tasks >= region->waiting_readers) {
            region->header.waiting_tasks -= region->waiting_readers;
        } else {
            region->header.waiting_tasks = 0;
        }
        region->waiting_readers = 0;
        break;
    case IPC_SHM_CONTROL_NOTIFY_WRITERS:
        ipc_wake_all(&region->write_waiters, IPC_WAIT_RESULT_OK);
        if (region->header.waiting_tasks >= region->waiting_writers) {
            region->header.waiting_tasks -= region->waiting_writers;
        } else {
            region->header.waiting_tasks = 0;
        }
        region->waiting_writers = 0;
        break;
    default:
        portEXIT_CRITICAL(&region->header.lock);
        return IPC_ERR_INVALID_ARGUMENT;
    }

    portEXIT_CRITICAL(&region->header.lock);
    return IPC_OK;
}

/**
 * @brief   Return the raw payload pointer for the region.
 */
static inline uint8_t *ipc_shm_memory_ptr(const ipc_shm_region_t *region)
{
    return (uint8_t *)region->memory;
}

static void ipc_shm_memcpy_to_region(ipc_shm_region_t *region,
                                     size_t offset,
                                     const void *src,
                                     size_t length)
{
    if (length == 0 || region->memory == NULL) {
        return;
    }

    size_t normalized = offset % region->region_size;
    size_t headspace = region->region_size - normalized;
    const uint8_t *source = src;

    if (headspace >= length) {
        memcpy(ipc_shm_memory_ptr(region) + normalized, source, length);
        return;
    }

    memcpy(ipc_shm_memory_ptr(region) + normalized, source, headspace);
    memcpy(ipc_shm_memory_ptr(region),
           source + headspace,
           length - headspace);
}

static void ipc_shm_memcpy_from_region(ipc_shm_region_t *region,
                                       size_t offset,
                                       void *dest,
                                       size_t length)
{
    if (length == 0 || region->memory == NULL) {
        return;
    }

    size_t normalized = offset % region->region_size;
    size_t headspace = region->region_size - normalized;
    uint8_t *target = dest;

    if (headspace >= length) {
        memcpy(target, ipc_shm_memory_ptr(region) + normalized, length);
        return;
    }

    memcpy(target, ipc_shm_memory_ptr(region) + normalized, headspace);
    memcpy(target + headspace, ipc_shm_memory_ptr(region), length - headspace);
}

/**
 * @brief   Reset region counters and wait queues for reuse.
 */
static void ipc_shm_reset_state(ipc_shm_region_t *region)
{
    region->ring_head = 0;
    region->ring_tail = 0;
    region->ring_used = 0;
    region->packet_head = 0;
    region->packet_tail = 0;
    region->packet_count = 0;
    region->packet_bytes = 0;
    region->raw_ready = true;
    region->waiting_readers = 0;
    region->waiting_writers = 0;
    region->stats = (ipc_shm_stats_t){0};
    ipc_wait_queue_init(&region->read_waiters);
    ipc_wait_queue_init(&region->write_waiters);
    region->attachment_count = 0;
    region->header.destroyed = false;
    region->header.waiting_tasks = 0;
}

/**
 * @brief   Zero the buffer heads and counters without touching waiters.
 */
static void ipc_shm_clear_contents(ipc_shm_region_t *region)
{
    region->ring_head = 0;
    region->ring_tail = 0;
    region->ring_used = 0;
    region->packet_head = 0;
    region->packet_tail = 0;
    region->packet_count = 0;
    region->packet_bytes = 0;
}

/**
 * @brief   Track a new waiter that entered the queue.
 */
static void ipc_shm_after_enqueue(ipc_shm_region_t *region)
{
    region->header.waiting_tasks++;
}

/**
 * @brief   Track a waiter that left the queue.
 */
static void ipc_shm_after_dequeue(ipc_shm_region_t *region)
{
    if (region->header.waiting_tasks > 0) {
        region->header.waiting_tasks--;
    }
}

static ipc_error_t ipc_shm_convert_wait_result(ipc_shm_region_t *region,
                                               ipc_wait_result_t wait_result,
                                               bool read)
{
    switch (wait_result) {
    case IPC_WAIT_RESULT_OK:
        return IPC_OK;
    case IPC_WAIT_RESULT_TIMEOUT:
        if (read) {
            region->stats.read_timeouts++;
        } else {
            region->stats.write_timeouts++;
        }
        return IPC_ERR_TIMEOUT;
    case IPC_WAIT_RESULT_OBJECT_DESTROYED:
        return IPC_ERR_OBJECT_DESTROYED;
    default:
        return IPC_ERR_SHUTDOWN;
    }
}

static ipc_error_t ipc_shm_attachment_validate(ipc_shm_attachment_t *attachment,
                                               ipc_shm_region_t **out_region)
{
    if (attachment == NULL || !attachment->attached
        || attachment->internal == NULL) {
        return IPC_ERR_NOT_ATTACHED;
    }

    ipc_shm_region_t *region = (ipc_shm_region_t *)attachment->internal;
    if (region == NULL || region->header.handle != attachment->handle) {
        return IPC_ERR_INVALID_HANDLE;
    }

    if (out_region != NULL) {
        *out_region = region;
    }

    return IPC_OK;
}

/**
 * @brief   Free the region memory once it is destroyed and orphaned.
 */
static bool ipc_shm_cleanup_locked(ipc_shm_region_t *region,
                                   ipc_handle_t *out_handle)
{
    if (region == NULL || !region->header.destroyed
        || region->attachment_count != 0) {
        return false;
    }

    if (region->memory != NULL) {
        vPortFree(region->memory);
        region->memory = NULL;
    }

    ipc_shm_reset_state(region);
    if (out_handle != NULL) {
        *out_handle = region->header.handle;
    }
    region->header.handle = IPC_HANDLE_INVALID;
    region->header.generation = 0;
    region->header.type = IPC_OBJECT_NONE;
    return true;
}
