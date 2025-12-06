#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"

#include "kernel/core/ipc/ipc_shm.h"
#include "kernel/core/vfs/core/m_vfs_errno.h"
#include "kernel/vfs/fs/devfs/devfs.h"
#include "kernel/vfs/fs/devfs/devfs_stream.h"

static const char *const STREAM_TAG = "devfs_stream";

static m_vfs_error_t
devfs_stream_map_ipc_error(ipc_error_t err)
{
    switch (err) {
        case IPC_OK:
            return M_VFS_ERR_OK;
        case IPC_ERR_EMPTY:
        case IPC_ERR_FULL:
        case IPC_ERR_NO_SPACE:
            return M_VFS_ERR_WOULD_BLOCK;
        case IPC_ERR_TIMEOUT:
            return M_VFS_ERR_TIMEOUT;
        case IPC_ERR_OBJECT_DESTROYED:
        case IPC_ERR_SHUTDOWN:
            return M_VFS_ERR_DESTROYED;
        case IPC_ERR_INVALID_ARGUMENT:
        case IPC_ERR_INVALID_HANDLE:
        case IPC_ERR_NOT_ATTACHED:
        case IPC_ERR_NO_PERMISSION:
            return M_VFS_ERR_INVALID_PARAM;
        default:
            return M_VFS_ERR_IO;
    }
}

static devfs_event_mask_t
devfs_stream_compute_ready_mask(devfs_stream_context_t *ctx)
{
    if (ctx == NULL || ctx->handle == IPC_HANDLE_INVALID) {
        return DEVFS_EVENT_ERROR;
    }

    ipc_shm_info_t info = {0};
    ipc_error_t err = ipc_shm_query(ctx->handle, &info);
    if (err != IPC_OK) {
        ESP_LOGW(STREAM_TAG, "Failed to query SHM %s (%d)",
                 ctx->path != NULL ? ctx->path : "<unknown>",
                 err);
        return DEVFS_EVENT_ERROR;
    }

    devfs_event_mask_t mask = 0;
    if (info.ring_used > 0) {
        mask |= DEVFS_EVENT_READABLE;
    }
    if (info.ring_used < info.ring_capacity) {
        mask |= DEVFS_EVENT_WRITABLE;
    }

    return mask;
}

static void
devfs_stream_refresh_ready(devfs_stream_context_t *ctx, bool force_notify)
{
    if (ctx == NULL) {
        return;
    }

    devfs_event_mask_t mask = devfs_stream_compute_ready_mask(ctx);

    portENTER_CRITICAL(&ctx->lock);
    devfs_event_mask_t previous = ctx->ready_mask;
    ctx->ready_mask = mask;
    m_vfs_node_t *node = ctx->node;
    portEXIT_CRITICAL(&ctx->lock);

    if (node != NULL && (force_notify || mask != previous)) {
        devfs_notify(node, mask);
    }
}

bool
devfs_stream_context_init(devfs_stream_context_t *ctx,
                          const char *path,
                          size_t buffer_size,
                          ipc_shm_ring_overwrite_policy_t policy)
{
    if (ctx == NULL || path == NULL || buffer_size == 0) {
        return false;
    }

    memset(ctx, 0, sizeof(*ctx));
    ctx->lock = (portMUX_TYPE)portMUX_INITIALIZER_UNLOCKED;
    ctx->path = path;
    ctx->buffer_capacity = buffer_size;
    ctx->policy = policy;
    ctx->handle = IPC_HANDLE_INVALID;

    ipc_shm_region_options_t options = {
        .ring_policy = policy,
    };
    ipc_error_t err = ipc_shm_create(buffer_size,
                                     IPC_SHM_MODE_RING_BUFFER,
                                     &options,
                                     &ctx->handle);
    if (err != IPC_OK) {
        ESP_LOGE(STREAM_TAG,
                 "Failed to create SHM region for %s (%d)",
                 path,
                 err);
        return false;
    }

    err = ipc_shm_attach(ctx->handle,
                         IPC_SHM_ACCESS_READ_ONLY,
                         NULL,
                         &ctx->reader);
    if (err != IPC_OK) {
        ESP_LOGE(STREAM_TAG,
                 "Failed to attach reader for %s (%d)",
                 path,
                 err);
        devfs_stream_context_cleanup(ctx);
        return false;
    }

    err = ipc_shm_attach(ctx->handle,
                         IPC_SHM_ACCESS_WRITE_ONLY,
                         NULL,
                         &ctx->writer);
    if (err != IPC_OK) {
        ESP_LOGE(STREAM_TAG,
                 "Failed to attach writer for %s (%d)",
                 path,
                 err);
        devfs_stream_context_cleanup(ctx);
        return false;
    }

    devfs_stream_refresh_ready(ctx, true);
    return true;
}

void
devfs_stream_context_cleanup(devfs_stream_context_t *ctx)
{
    if (ctx == NULL) {
        return;
    }

    if (ctx->reader.attached) {
        ipc_shm_detach(&ctx->reader);
    }
    if (ctx->writer.attached) {
        ipc_shm_detach(&ctx->writer);
    }
    if (ctx->handle != IPC_HANDLE_INVALID) {
        ipc_shm_destroy(ctx->handle);
        ctx->handle = IPC_HANDLE_INVALID;
    }

    portENTER_CRITICAL(&ctx->lock);
    ctx->node = NULL;
    ctx->ready_mask = 0;
    portEXIT_CRITICAL(&ctx->lock);
}

void
devfs_stream_attach_node(devfs_stream_context_t *ctx, m_vfs_node_t *node)
{
    if (ctx == NULL) {
        return;
    }

    portENTER_CRITICAL(&ctx->lock);
    ctx->node = node;
    portEXIT_CRITICAL(&ctx->lock);
    devfs_stream_refresh_ready(ctx, true);
}

void
devfs_stream_detach_node(devfs_stream_context_t *ctx)
{
    if (ctx == NULL) {
        return;
    }

    portENTER_CRITICAL(&ctx->lock);
    ctx->node = NULL;
    ctx->ready_mask = 0;
    portEXIT_CRITICAL(&ctx->lock);
}

m_vfs_error_t
devfs_stream_try_read(devfs_stream_context_t *ctx,
                      void *buffer,
                      size_t size,
                      size_t *read)
{
    if (ctx == NULL || buffer == NULL || read == NULL) {
        return M_VFS_ERR_INVALID_PARAM;
    }
    if (ctx->handle == IPC_HANDLE_INVALID) {
        return M_VFS_ERR_DESTROYED;
    }

    size_t transferred = 0;
    ipc_error_t err = ipc_shm_try_read(&ctx->reader, buffer, size, &transferred);
    if (err == IPC_OK) {
        *read = transferred;
    } else {
        *read = 0;
    }

    devfs_stream_refresh_ready(ctx, false);
    if (err == IPC_OK) {
        return M_VFS_ERR_OK;
    }
    return devfs_stream_map_ipc_error(err);
}

m_vfs_error_t
devfs_stream_try_write(devfs_stream_context_t *ctx,
                       const void *buffer,
                       size_t size,
                       size_t *written)
{
    if (ctx == NULL || buffer == NULL || written == NULL) {
        return M_VFS_ERR_INVALID_PARAM;
    }
    if (ctx->handle == IPC_HANDLE_INVALID) {
        return M_VFS_ERR_DESTROYED;
    }

    ipc_error_t err = ipc_shm_try_write(&ctx->writer, buffer, size);
    if (err == IPC_OK) {
        *written = size;
    } else {
        *written = 0;
    }

    devfs_stream_refresh_ready(ctx, false);
    if (err == IPC_OK) {
        return M_VFS_ERR_OK;
    }
    return devfs_stream_map_ipc_error(err);
}

m_vfs_error_t
devfs_stream_read_timed(devfs_stream_context_t *ctx,
                        void *buffer,
                        size_t size,
                        size_t *read,
                        uint64_t timeout_us)
{
    if (ctx == NULL || buffer == NULL || read == NULL) {
        return M_VFS_ERR_INVALID_PARAM;
    }
    if (ctx->handle == IPC_HANDLE_INVALID) {
        return M_VFS_ERR_DESTROYED;
    }

    size_t transferred = 0;
    ipc_error_t err = ipc_shm_read_timed(&ctx->reader, buffer, size, &transferred, timeout_us);
    if (err == IPC_OK) {
        *read = transferred;
    } else {
        *read = 0;
    }

    devfs_stream_refresh_ready(ctx, false);
    if (err == IPC_OK) {
        return M_VFS_ERR_OK;
    }
    return devfs_stream_map_ipc_error(err);
}

m_vfs_error_t
devfs_stream_write_timed(devfs_stream_context_t *ctx,
                         const void *buffer,
                         size_t size,
                         size_t *written,
                         uint64_t timeout_us)
{
    if (ctx == NULL || buffer == NULL || written == NULL) {
        return M_VFS_ERR_INVALID_PARAM;
    }
    if (ctx->handle == IPC_HANDLE_INVALID) {
        return M_VFS_ERR_DESTROYED;
    }

    ipc_error_t err = ipc_shm_write_timed(&ctx->writer, buffer, size, timeout_us);
    if (err == IPC_OK) {
        *written = size;
    } else {
        *written = 0;
    }

    devfs_stream_refresh_ready(ctx, false);
    if (err == IPC_OK) {
        return M_VFS_ERR_OK;
    }
    return devfs_stream_map_ipc_error(err);
}

uint32_t
devfs_stream_poll(const devfs_stream_context_t *ctx)
{
    if (ctx == NULL) {
        return 0;
    }
    devfs_event_mask_t mask = devfs_stream_ready_mask(ctx);
    return (uint32_t)mask;
}

devfs_event_mask_t
devfs_stream_ready_mask(const devfs_stream_context_t *ctx)
{
    if (ctx == NULL) {
        return 0;
    }

    portENTER_CRITICAL((portMUX_TYPE *)&ctx->lock);
    devfs_event_mask_t mask = ctx->ready_mask;
    portEXIT_CRITICAL((portMUX_TYPE *)&ctx->lock);
    return mask;
}

m_vfs_error_t
devfs_stream_buffer_info(const devfs_stream_context_t *ctx,
                         devfs_shm_buffer_info_t *info)
{
    if (ctx == NULL || info == NULL) {
        return M_VFS_ERR_INVALID_PARAM;
    }
    if (ctx->handle == IPC_HANDLE_INVALID) {
        return M_VFS_ERR_DESTROYED;
    }

    ipc_shm_info_t shm_info = {0};
    ipc_error_t err = ipc_shm_query(ctx->handle, &shm_info);
    if (err != IPC_OK) {
        return M_VFS_ERR_IO;
    }

    info->used = shm_info.ring_used;
    info->capacity = shm_info.ring_capacity;
    return M_VFS_ERR_OK;
}

m_vfs_error_t
devfs_stream_control(devfs_stream_context_t *ctx,
                     ipc_shm_control_command_t cmd,
                     void *arg)
{
    if (ctx == NULL || ctx->handle == IPC_HANDLE_INVALID) {
        return M_VFS_ERR_DESTROYED;
    }

    ipc_error_t err = ipc_shm_control(ctx->handle, cmd, arg);
    devfs_stream_refresh_ready(ctx, true);
    if (err == IPC_OK) {
        return M_VFS_ERR_OK;
    }
    return devfs_stream_map_ipc_error(err);
}
