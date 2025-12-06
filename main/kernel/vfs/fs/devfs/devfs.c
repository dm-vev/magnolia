#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"

#include "sdkconfig.h"
#include "kernel/core/vfs/core/m_vfs_object.h"
#include "kernel/core/vfs/core/m_vfs_errno.h"
#include "kernel/core/vfs/core/m_vfs_wait.h"
#include "kernel/core/vfs/fd/m_vfs_fd.h"
#include "kernel/core/vfs/m_vfs.h"
#include "kernel/core/vfs/path/m_vfs_path.h"
#include "kernel/vfs/fs/devfs/devfs.h"
#include "kernel/vfs/fs/devfs/devfs_internal.h"
#include "kernel/vfs/fs/devfs/devfs_diag.h"
#include "kernel/vfs/fs/devfs/devfs_ioctl.h"
#include "kernel/vfs/fs/devfs/devfs_shm.h"

#if CONFIG_MAGNOLIA_IPC_ENABLED
#include "kernel/core/ipc/ipc_shm.h"
#endif

#if CONFIG_MAGNOLIA_IPC_ENABLED

static const char *const DEVFS_SHM_TAG = "devfs_shm";

typedef struct {
    const char *path;
    size_t size;
    ipc_shm_ring_overwrite_policy_t policy;
} devfs_shm_device_spec_t;

static const devfs_shm_device_spec_t s_devfs_shm_specs[] = {
    { "/dev/pipe0", 128, IPC_SHM_RING_OVERWRITE_BLOCK },
    { "/dev/stream0", 256, IPC_SHM_RING_OVERWRITE_DROP_OLDEST },
};

#define DEVFS_SHM_DEVICE_COUNT \
    (sizeof(s_devfs_shm_specs) / sizeof(s_devfs_shm_specs[0]))

typedef struct {
    ipc_handle_t handle;
    ipc_shm_attachment_t reader;
    ipc_shm_attachment_t writer;
    m_vfs_node_t *node;
    portMUX_TYPE lock;
    devfs_event_mask_t ready_mask;
    const char *path;
} devfs_shm_device_context_t;

static devfs_shm_device_context_t g_devfs_shm_contexts[DEVFS_SHM_DEVICE_COUNT];

static devfs_shm_device_context_t *
devfs_shm_context_from_entry(const devfs_entry_t *entry)
{
    if (entry == NULL || entry->private_data == NULL) {
        return NULL;
    }

    devfs_shm_device_context_t *ctx =
            (devfs_shm_device_context_t *)entry->private_data;
    for (size_t i = 0; i < DEVFS_SHM_DEVICE_COUNT; ++i) {
        if (&g_devfs_shm_contexts[i] == ctx) {
            return ctx;
        }
    }
    return NULL;
}

static devfs_event_mask_t
devfs_shm_compute_ready_mask(devfs_shm_device_context_t *ctx)
{
    if (ctx == NULL || ctx->handle == IPC_HANDLE_INVALID) {
        return DEVFS_EVENT_ERROR;
    }

    ipc_shm_info_t info = {0};
    ipc_error_t err = ipc_shm_query(ctx->handle, &info);
    if (err != IPC_OK) {
        ESP_LOGE(DEVFS_SHM_TAG,
                 "shm query failed for %s (%d)",
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
devfs_shm_refresh_ready(devfs_shm_device_context_t *ctx, bool force_notify)
{
    if (ctx == NULL) {
        return;
    }

    devfs_event_mask_t mask = devfs_shm_compute_ready_mask(ctx);

    portENTER_CRITICAL(&ctx->lock);
    devfs_event_mask_t previous = ctx->ready_mask;
    ctx->ready_mask = mask;
    m_vfs_node_t *node = ctx->node;
    portEXIT_CRITICAL(&ctx->lock);

    if (node != NULL && (force_notify || mask != previous)) {
        devfs_notify(node, mask);
    }
}

static void
devfs_shm_cleanup_context(devfs_shm_device_context_t *ctx)
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
}

static bool
devfs_shm_setup_context(devfs_shm_device_context_t *ctx,
                        const devfs_shm_device_spec_t *spec)
{
    if (ctx == NULL || spec == NULL) {
        return false;
    }

    ctx->handle = IPC_HANDLE_INVALID;
    ipc_shm_region_options_t options = {
        .ring_policy = spec->policy,
    };
    ipc_error_t err = ipc_shm_create(spec->size,
                                     IPC_SHM_MODE_RING_BUFFER,
                                     &options,
                                     &ctx->handle);
    if (err != IPC_OK) {
        ESP_LOGE(DEVFS_SHM_TAG,
                 "Failed to create %s region (%d)",
                 spec->path,
                 err);
        return false;
    }

    err = ipc_shm_attach(ctx->handle,
                         IPC_SHM_ACCESS_READ_ONLY,
                         NULL,
                         &ctx->reader);
    if (err != IPC_OK) {
        ESP_LOGE(DEVFS_SHM_TAG,
                 "Failed to attach reader for %s (%d)",
                 spec->path,
                 err);
        devfs_shm_cleanup_context(ctx);
        return false;
    }

    err = ipc_shm_attach(ctx->handle,
                         IPC_SHM_ACCESS_WRITE_ONLY,
                         NULL,
                         &ctx->writer);
    if (err != IPC_OK) {
        ESP_LOGE(DEVFS_SHM_TAG,
                 "Failed to attach writer for %s (%d)",
                 spec->path,
                 err);
        devfs_shm_cleanup_context(ctx);
        return false;
    }

    return true;
}

static m_vfs_error_t
devfs_shm_map_ipc_error(ipc_error_t err)
{
    switch (err) {
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

static m_vfs_error_t
devfs_shm_read(void *private_data,
               void *buffer,
               size_t size,
               size_t *read)
{
    if (private_data == NULL || buffer == NULL || read == NULL) {
        return M_VFS_ERR_INVALID_PARAM;
    }

    if (size == 0) {
        *read = 0;
        return M_VFS_ERR_OK;
    }

    devfs_shm_device_context_t *ctx = (devfs_shm_device_context_t *)private_data;
    size_t transferred = 0;
    ipc_error_t err = ipc_shm_try_read(&ctx->reader, buffer, size, &transferred);
    devfs_shm_refresh_ready(ctx, false);

    if (err == IPC_OK) {
        *read = transferred;
        return M_VFS_ERR_OK;
    }

    *read = 0;
    if (err == IPC_ERR_EMPTY) {
        return M_VFS_ERR_WOULD_BLOCK;
    }
    return devfs_shm_map_ipc_error(err);
}

static m_vfs_error_t
devfs_shm_write(void *private_data,
                const void *buffer,
                size_t size,
                size_t *written)
{
    if (private_data == NULL || buffer == NULL || written == NULL) {
        return M_VFS_ERR_INVALID_PARAM;
    }

    if (size == 0) {
        *written = 0;
        return M_VFS_ERR_OK;
    }

    devfs_shm_device_context_t *ctx = (devfs_shm_device_context_t *)private_data;
    ipc_error_t err = ipc_shm_try_write(&ctx->writer, buffer, size);
    devfs_shm_refresh_ready(ctx, false);

    if (err == IPC_OK) {
        *written = size;
        return M_VFS_ERR_OK;
    }

    *written = 0;
    if (err == IPC_ERR_FULL || err == IPC_ERR_NO_SPACE) {
        return M_VFS_ERR_WOULD_BLOCK;
    }

    return devfs_shm_map_ipc_error(err);
}

static uint32_t
devfs_shm_poll(void *private_data)
{
    devfs_shm_device_context_t *ctx = (devfs_shm_device_context_t *)private_data;
    return (ctx == NULL) ? 0 : devfs_shm_compute_ready_mask(ctx);
}

static m_vfs_error_t
devfs_shm_ioctl(void *private_data,
                unsigned long request,
                void *arg)
{
    if (private_data == NULL || arg == NULL) {
        return M_VFS_ERR_INVALID_PARAM;
    }

    if (request != DEVFS_SHM_IOCTL_BUFFER_INFO) {
        return M_VFS_ERR_NOT_SUPPORTED;
    }

    devfs_shm_device_context_t *ctx = (devfs_shm_device_context_t *)private_data;
    devfs_shm_buffer_info_t *info = (devfs_shm_buffer_info_t *)arg;

    ipc_shm_info_t shm_info = {0};
    if (ipc_shm_query(ctx->handle, &shm_info) != IPC_OK) {
        return M_VFS_ERR_IO;
    }

    info->used = shm_info.ring_used;
    info->capacity = shm_info.ring_capacity;
    return M_VFS_ERR_OK;
}

static void
devfs_shm_attach_node(const devfs_entry_t *entry,
                      devfs_device_node_t *record)
{
    devfs_shm_device_context_t *ctx = devfs_shm_context_from_entry(entry);
    if (ctx == NULL || record == NULL) {
        return;
    }

    portENTER_CRITICAL(&ctx->lock);
    ctx->node = record->node;
    portEXIT_CRITICAL(&ctx->lock);
    devfs_shm_refresh_ready(ctx, true);
}

static void
devfs_shm_detach_node(const devfs_entry_t *entry,
                      devfs_device_node_t *record)
{
    (void)record;
    devfs_shm_device_context_t *ctx = devfs_shm_context_from_entry(entry);
    if (ctx == NULL) {
        return;
    }

    portENTER_CRITICAL(&ctx->lock);
    ctx->node = NULL;
    ctx->ready_mask = 0;
    portEXIT_CRITICAL(&ctx->lock);
}

static const devfs_ops_t s_devfs_shm_ops = {
    .read = devfs_shm_read,
    .write = devfs_shm_write,
    .ioctl = devfs_shm_ioctl,
    .poll = devfs_shm_poll,
};

static void
devfs_shm_register_devices(void)
{
    for (size_t i = 0; i < DEVFS_SHM_DEVICE_COUNT; ++i) {
        devfs_shm_device_context_t *ctx = &g_devfs_shm_contexts[i];
        const devfs_shm_device_spec_t *spec = &s_devfs_shm_specs[i];
        memset(ctx, 0, sizeof(*ctx));
        ctx->lock = (portMUX_TYPE)portMUX_INITIALIZER_UNLOCKED;
        ctx->path = spec->path;

        if (!devfs_shm_setup_context(ctx, spec)) {
            continue;
        }

        m_vfs_error_t err = devfs_register_ext(spec->path,
                                               &s_devfs_shm_ops,
                                               ctx,
                                               devfs_shm_attach_node,
                                               devfs_shm_detach_node);
        if (err != M_VFS_ERR_OK) {
            ESP_LOGE(DEVFS_SHM_TAG,
                     "Failed to register %s (%d)",
                     spec->path,
                     err);
            devfs_shm_cleanup_context(ctx);
        }
    }
}

#endif /* CONFIG_MAGNOLIA_IPC_ENABLED */

#if CONFIG_MAGNOLIA_VFS_DEVFS

#define DEVFS_MAX_MOUNTS CONFIG_MAGNOLIA_VFS_MAX_MOUNTS

#else

#define DEVFS_MAX_MOUNTS 0

#endif /* CONFIG_MAGNOLIA_VFS_DEVFS */

#if CONFIG_MAGNOLIA_VFS_DEVFS
const devfs_entry_t *
devfs_entry_from_node(const m_vfs_node_t *node)
{
    if (node == NULL || node->fs_private == NULL) {
        return NULL;
    }

    const devfs_node_data_t *data =
            (const devfs_node_data_t *)node->fs_private;
    return data->entry;
}
#endif

#if CONFIG_MAGNOLIA_VFS_DEVFS
static const struct m_vfs_fs_ops s_devfs_noops = {0};
static const m_vfs_fs_type_t s_devfs_fs_type = {
    .name = "devfs",
    .ops = &s_devfs_noops,
    .cookie = NULL,
};

const m_vfs_fs_type_t *
m_devfs_fs_type(void)
{
    return &s_devfs_fs_type;
}

void
m_devfs_register_default_devices(void)
{
#if CONFIG_MAGNOLIA_IPC_ENABLED
    devfs_shm_register_devices();
#endif
}

m_vfs_error_t
devfs_register(const char *path,
               const devfs_ops_t *ops,
               void *private_data)
{
    (void)path;
    (void)ops;
    (void)private_data;
    return M_VFS_ERR_NOT_SUPPORTED;
}

m_vfs_error_t
devfs_register_ext(const char *path,
                   const devfs_ops_t *ops,
                   void *private_data,
                   void (*node_attach)(const devfs_entry_t *,
                                       devfs_device_node_t *),
                   void (*node_detach)(const devfs_entry_t *,
                                       devfs_device_node_t *))
{
    (void)path;
    (void)ops;
    (void)private_data;
    (void)node_attach;
    (void)node_detach;
    return M_VFS_ERR_NOT_SUPPORTED;
}

m_vfs_error_t
devfs_unregister(const char *path)
{
    (void)path;
    return M_VFS_ERR_NOT_SUPPORTED;
}

void
devfs_notify(m_vfs_node_t *node, devfs_event_mask_t events)
{
    (void)node;
    (void)events;
}

devfs_event_mask_t
devfs_event_mask(const m_vfs_node_t *node)
{
    (void)node;
    return 0;
}

void
devfs_record_poll(const m_vfs_node_t *node)
{
    (void)node;
}

void
devfs_diag_device_iterate(devfs_diag_device_iter_fn cb, void *user_data)
{
    (void)cb;
    (void)user_data;
}

void
devfs_diag_tree_snapshot(devfs_diag_tree_iter_fn cb, void *user_data)
{
    (void)cb;
    (void)user_data;
}

void
devfs_diag_waiters(devfs_diag_waiter_iter_fn cb, void *user_data)
{
    (void)cb;
    (void)user_data;
}

void
devfs_diag_shm_info(devfs_diag_shm_iter_fn cb, void *user_data)
{
    (void)cb;
    (void)user_data;
}

size_t
devfs_diag_unregister_events(void)
{
    return 0;
}

size_t
devfs_diag_total_poll_count(void)
{
    return 0;
}
#endif
