#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdatomic.h>
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
#include "kernel/vfs/fs/devfs/devfs_stream.h"
#include "esp_random.h"

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

static devfs_shm_device_context_t *devfs_shm_context_from_entry(const devfs_entry_t *entry)
{
    if (entry == NULL || entry->private_data == NULL) {
        return NULL;
    }

    devfs_shm_device_context_t *ctx = (devfs_shm_device_context_t *)entry->private_data;
    for (size_t i = 0; i < DEVFS_SHM_DEVICE_COUNT; ++i) {
        if (&g_devfs_shm_contexts[i] == ctx) {
            return ctx;
        }
    }
    return NULL;
}

static devfs_event_mask_t devfs_shm_compute_ready_mask(devfs_shm_device_context_t *ctx)
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

static void devfs_shm_refresh_ready(devfs_shm_device_context_t *ctx, bool force_notify)
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

static void devfs_shm_cleanup_context(devfs_shm_device_context_t *ctx)
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

static bool devfs_shm_setup_context(devfs_shm_device_context_t *ctx,
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

static m_vfs_error_t devfs_shm_map_ipc_error(ipc_error_t err)
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
    case IPC_ERR_INVALID_HANDLE:
    case IPC_ERR_INVALID_ARGUMENT:
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

    devfs_shm_device_context_t *ctx =
            (devfs_shm_device_context_t *)private_data;
    if (ctx == NULL || ctx->reader.handle == IPC_HANDLE_INVALID) {
        return M_VFS_ERR_DESTROYED;
    }

    ipc_error_t err = ipc_shm_try_read(&ctx->reader, buffer, size, read);
    if (err == IPC_ERR_WOULD_BLOCK || err == IPC_ERR_EMPTY) {
        *read = 0;
        return M_VFS_ERR_WOULD_BLOCK;
    }
    if (err != IPC_OK) {
        *read = 0;
        return devfs_shm_map_ipc_error(err);
    }

    portENTER_CRITICAL(&ctx->lock);
    ctx->ready_mask = devfs_shm_compute_ready_mask(ctx);
    portEXIT_CRITICAL(&ctx->lock);
    devfs_notify(ctx->node, ctx->ready_mask);
    return M_VFS_ERR_OK;
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

    devfs_shm_device_context_t *ctx =
            (devfs_shm_device_context_t *)private_data;
    if (ctx == NULL || ctx->writer.handle == IPC_HANDLE_INVALID) {
        return M_VFS_ERR_DESTROYED;
    }

    ipc_error_t err = ipc_shm_try_write(&ctx->writer, buffer, size);
    if (err == IPC_OK) {
        *written = size;
    } else {
        *written = 0;
        if (err == IPC_ERR_WOULD_BLOCK
            || err == IPC_ERR_FULL
            || err == IPC_ERR_NO_SPACE) {
            return M_VFS_ERR_WOULD_BLOCK;
        }
        return devfs_shm_map_ipc_error(err);
    }

    portENTER_CRITICAL(&ctx->lock);
    ctx->ready_mask = devfs_shm_compute_ready_mask(ctx);
    portEXIT_CRITICAL(&ctx->lock);
    devfs_notify(ctx->node, ctx->ready_mask);
    return M_VFS_ERR_OK;
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

static void devfs_shm_attach_node(const devfs_entry_t *entry,
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

static void devfs_shm_detach_node(const devfs_entry_t *entry,
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

static void devfs_shm_register_devices(void)
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

#define DEVFS_CHILD_CAPACITY CONFIG_MAGNOLIA_DEVFS_MAX_DEVICES

typedef struct {
    char name[M_VFS_NAME_MAX_LEN];
    bool is_directory;
} devfs_child_t;

static atomic_size_t s_devfs_poll_total = ATOMIC_VAR_INIT(0);
static atomic_size_t s_devfs_unregister_events = ATOMIC_VAR_INIT(0);
static devfs_entry_t *s_devfs_entries = NULL;
static size_t s_devfs_entry_count = 0;
static devfs_mount_data_t *s_devfs_mounts = NULL;
static portMUX_TYPE s_devfs_lock =
        (portMUX_TYPE)portMUX_INITIALIZER_UNLOCKED;

static const char *devfs_basename(const char *path)
{
    if (path == NULL || *path == '\0') {
        return path;
    }

    const char *last = strrchr(path, '/');
    if (last == NULL) {
        return path;
    }
    if (*(last + 1) == '\0') {
        return path;
    }
    return last + 1;
}

static bool devfs_join_path(const char *parent,
                            const char *name,
                            char *out,
                            size_t len)
{
    if (parent == NULL || name == NULL || out == NULL || name[0] == '\0') {
        return false;
    }

    if (strcmp(parent, "/") == 0) {
        if (snprintf(out, len, "/%s", name) >= (int)len) {
            return false;
        }
    } else {
        if (snprintf(out, len, "%s/%s", parent, name) >= (int)len) {
            return false;
        }
    }
    return true;
}

static bool devfs_path_is_child(const char *path, const char *parent)
{
    if (path == NULL || parent == NULL) {
        return false;
    }

    size_t parent_len = strnlen(parent, M_VFS_PATH_MAX_LEN);
    if (parent_len == 0) {
        return false;
    }

    if (parent_len == 1 && parent[0] == '/') {
        if (path[0] != '/') {
            return false;
        }
        return path[1] != '\0';
    }

    if (strncmp(path, parent, parent_len) != 0) {
        return false;
    }

    if (path[parent_len] != '/') {
        return false;
    }

    return path[parent_len + 1] != '\0';
}

static devfs_entry_t *devfs_entry_find_locked(const char *path)
{
    devfs_entry_t *iter = s_devfs_entries;
    while (iter != NULL) {
        if (strcmp(iter->path, path) == 0) {
            return iter;
        }
        iter = iter->next;
    }
    return NULL;
}

static bool devfs_has_children_locked(const char *path)
{
    devfs_entry_t *iter = s_devfs_entries;
    while (iter != NULL) {
        if (devfs_path_is_child(iter->path, path)) {
            return true;
        }
        iter = iter->next;
    }
    return false;
}

static bool devfs_child_exists(const devfs_child_t *children,
                               size_t count,
                               const char *name,
                               bool is_directory)
{
    for (size_t i = 0; i < count; ++i) {
        if (strcmp(children[i].name, name) == 0 &&
                children[i].is_directory == is_directory) {
            return true;
        }
    }
    return false;
}

static int devfs_child_cmp(const void *a, const void *b)
{
    const devfs_child_t *lhs = (const devfs_child_t *)a;
    const devfs_child_t *rhs = (const devfs_child_t *)b;
    return strcmp(lhs->name, rhs->name);
}

static size_t devfs_collect_children_locked(const char *parent,
                                            devfs_child_t *out,
                                            size_t capacity)
{
    size_t count = 0;
    size_t parent_len = strnlen(parent, M_VFS_PATH_MAX_LEN);
    devfs_entry_t *iter = s_devfs_entries;
    while (iter != NULL) {
        if (!devfs_path_is_child(iter->path, parent)) {
            iter = iter->next;
            continue;
        }

        const char *suffix = iter->path + parent_len;
        if (*suffix == '/') {
            ++suffix;
        }
        if (*suffix == '\0') {
            iter = iter->next;
            continue;
        }

        size_t segment_len = strcspn(suffix, "/");
        if (segment_len == 0 || segment_len >= M_VFS_NAME_MAX_LEN) {
            iter = iter->next;
            continue;
        }

        char name[M_VFS_NAME_MAX_LEN];
        memcpy(name, suffix, segment_len);
        name[segment_len] = '\0';

        bool is_directory = (suffix[segment_len] == '/');
        if (devfs_child_exists(out, count, name, is_directory)) {
            iter = iter->next;
            continue;
        }

        if (count < capacity) {
            strncpy(out[count].name, name, sizeof(out[count].name));
            out[count].name[sizeof(out[count].name) - 1] = '\0';
            out[count].is_directory = is_directory;
            ++count;
        }
        iter = iter->next;
    }

    if (count > 1) {
        qsort(out, count, sizeof(*out), devfs_child_cmp);
    }
    return count;
}

static void devfs_register_mount(devfs_mount_data_t *data)
{
    if (data == NULL) {
        return;
    }

    portENTER_CRITICAL(&s_devfs_lock);
    data->next = s_devfs_mounts;
    s_devfs_mounts = data;
    portEXIT_CRITICAL(&s_devfs_lock);
}

static void devfs_unregister_mount(devfs_mount_data_t *data)
{
    if (data == NULL) {
        return;
    }

    portENTER_CRITICAL(&s_devfs_lock);
    devfs_mount_data_t **slot = &s_devfs_mounts;
    while (*slot != NULL) {
        if (*slot == data) {
            *slot = data->next;
            data->next = NULL;
            break;
        }
        slot = &(*slot)->next;
    }
    portEXIT_CRITICAL(&s_devfs_lock);
}

static devfs_device_node_t *
devfs_mount_node_find(devfs_mount_data_t *mount, const char *path)
{
    if (mount == NULL || path == NULL) {
        return NULL;
    }

    devfs_device_node_t *iter = mount->nodes;
    while (iter != NULL) {
        devfs_node_data_t *data = (devfs_node_data_t *)iter->node->fs_private;
        if (data != NULL && strcmp(data->path, path) == 0) {
            return iter;
        }
        iter = iter->next_mount;
    }
    return NULL;
}

static void devfs_remove_device_from_mount(devfs_mount_data_t *mount,
                                           devfs_device_node_t *device)
{
    if (mount == NULL || device == NULL) {
        return;
    }

    devfs_device_node_t **slot = &mount->nodes;
    while (*slot != NULL) {
        if (*slot == device) {
            *slot = device->next_mount;
            device->next_mount = NULL;
            break;
        }
        slot = &(*slot)->next_mount;
    }
}

static void devfs_remove_device_from_entry(devfs_entry_t *entry,
                                           devfs_device_node_t *device)
{
    if (entry == NULL || device == NULL) {
        return;
    }

    devfs_device_node_t **slot = &entry->nodes;
    while (*slot != NULL) {
        if (*slot == device) {
            *slot = device->next_entry;
            device->next_entry = NULL;
            break;
        }
        slot = &(*slot)->next_entry;
    }
}

static m_vfs_node_t *
devfs_node_create(devfs_mount_data_t *mount_data,
                  m_vfs_node_t *parent,
                  const char *path,
                  const char *name,
                  devfs_entry_t *entry,
                  bool is_directory)
{
    if (mount_data == NULL || path == NULL || name == NULL) {
        return NULL;
    }

    m_vfs_node_type_t type = is_directory ? M_VFS_NODE_TYPE_DIRECTORY :
                                           M_VFS_NODE_TYPE_DEVICE;
    m_vfs_node_t *node = m_vfs_node_create(mount_data->mount, type);
    if (node == NULL) {
        return NULL;
    }

    devfs_node_data_t *node_data = pvPortMalloc(sizeof(*node_data));
    if (node_data == NULL) {
        m_vfs_node_release(node);
        return NULL;
    }

    devfs_device_node_t *device_record = pvPortMalloc(sizeof(*device_record));
    if (device_record == NULL) {
        vPortFree(node_data);
        m_vfs_node_release(node);
        return NULL;
    }

    memset(node_data, 0, sizeof(*node_data));
    memset(device_record, 0, sizeof(*device_record));
    node_data->entry = entry;
    node_data->device = device_record;
    node_data->is_directory = is_directory;
    strncpy(node_data->name, name, sizeof(node_data->name));
    node_data->name[sizeof(node_data->name) - 1] = '\0';
    strncpy(node_data->path, path, sizeof(node_data->path));
    node_data->path[sizeof(node_data->path) - 1] = '\0';

    device_record->node = node;
    device_record->entry = entry;
    device_record->mount = mount_data;
    device_record->is_directory = is_directory;
    device_record->lock = (portMUX_TYPE)portMUX_INITIALIZER_UNLOCKED;
    device_record->ready_mask = 0;

    device_record->next_mount = mount_data->nodes;
    mount_data->nodes = device_record;

    if (entry != NULL) {
        device_record->next_entry = entry->nodes;
        entry->nodes = device_record;
        entry->node_count++;
    }

    node->fs_private = node_data;
    if (parent != NULL) {
        node->parent = parent;
        m_vfs_node_acquire(parent);
    }

    return node;
}

static void devfs_maybe_free_mount(devfs_mount_data_t *mount)
{
    if (mount == NULL || !mount->pending_free) {
        return;
    }

    if (mount->nodes == NULL) {
        vPortFree(mount);
    }
}

static void devfs_node_destroy(m_vfs_node_t *node)
{
    if (node == NULL) {
        return;
    }

    devfs_node_data_t *data = (devfs_node_data_t *)node->fs_private;
    if (data == NULL) {
        vPortFree(node);
        return;
    }

    devfs_entry_t *entry = data->entry;
    devfs_device_node_t *device = data->device;
    devfs_mount_data_t *mount = (device != NULL) ? device->mount : NULL;
    void (*detach)(const devfs_entry_t *, devfs_device_node_t *) = NULL;
    if (entry != NULL && entry->node_detach != NULL) {
        detach = entry->node_detach;
    }

    if (mount != NULL) {
        portENTER_CRITICAL(&s_devfs_lock);
        portENTER_CRITICAL(&mount->lock);
        devfs_remove_device_from_mount(mount, device);
        if (node == mount->root) {
            mount->root = NULL;
        }
        if (entry != NULL) {
            devfs_remove_device_from_entry(entry, device);
            if (entry->node_count > 0) {
                --entry->node_count;
            }
        }
        portEXIT_CRITICAL(&mount->lock);
        portEXIT_CRITICAL(&s_devfs_lock);
    } else if (entry != NULL) {
        portENTER_CRITICAL(&s_devfs_lock);
        devfs_remove_device_from_entry(entry, device);
        if (entry->node_count > 0) {
            --entry->node_count;
        }
        portEXIT_CRITICAL(&s_devfs_lock);
    }

    if (node->parent != NULL) {
        m_vfs_node_release(node->parent);
    }

    if (detach != NULL) {
        detach(entry, device);
    }

    if (device != NULL) {
        vPortFree(device);
    }
    vPortFree(data);
    vPortFree(node);

    devfs_maybe_free_mount(mount);
}

const devfs_entry_t *devfs_entry_from_node(const m_vfs_node_t *node)
{
    if (node == NULL || node->fs_private == NULL) {
        return NULL;
    }

    const devfs_node_data_t *data = (const devfs_node_data_t *)node->fs_private;
    return data->entry;
}

typedef struct {
    const m_vfs_node_t *target;
} devfs_notify_ctx_t;

static bool devfs_notify_fd_cb(m_job_id_t job,
                               int fd,
                               const m_vfs_file_t *file,
                               void *user_data)
{
    (void)job;
    (void)fd;

    if (user_data == NULL || file == NULL) {
        return true;
    }

    devfs_notify_ctx_t *ctx = (devfs_notify_ctx_t *)user_data;
    if (file->node == ctx->target) {
        m_vfs_file_notify_event((m_vfs_file_t *)file);
    }
    return true;
}

void devfs_record_poll(const m_vfs_node_t *node)
{
    if (node == NULL || node->fs_private == NULL) {
        return;
    }

    devfs_node_data_t *data = (devfs_node_data_t *)node->fs_private;
    if (data == NULL || data->device == NULL) {
        return;
    }

    portENTER_CRITICAL(&data->device->lock);
    ++data->device->poll_count;
    portEXIT_CRITICAL(&data->device->lock);
    atomic_fetch_add_explicit(&s_devfs_poll_total, 1, memory_order_relaxed);
}

devfs_event_mask_t devfs_event_mask(const m_vfs_node_t *node)
{
    if (node == NULL || node->fs_private == NULL) {
        return 0;
    }

    devfs_node_data_t *data = (devfs_node_data_t *)node->fs_private;
    if (data == NULL || data->device == NULL) {
        return 0;
    }

    portENTER_CRITICAL(&data->device->lock);
    devfs_event_mask_t mask = data->device->ready_mask;
    portEXIT_CRITICAL(&data->device->lock);
    return mask;
}

void devfs_notify(m_vfs_node_t *node, devfs_event_mask_t events)
{
    if (node == NULL || node->fs_private == NULL) {
        return;
    }

    devfs_node_data_t *data = (devfs_node_data_t *)node->fs_private;
    if (data == NULL || data->device == NULL) {
        return;
    }

    bool should_wake = false;
    portENTER_CRITICAL(&data->device->lock);
    if (data->device->ready_mask != events ||
            (events & (DEVFS_EVENT_ERROR | DEVFS_EVENT_HANGUP)) != 0) {
        if (data->device->ready_mask != events) {
            data->device->ready_mask = events;
            ++data->device->notify_count;
        }
        should_wake = true;
    }
    portEXIT_CRITICAL(&data->device->lock);

    if (!should_wake) {
        return;
    }

    devfs_notify_ctx_t ctx = {
        .target = node,
    };
    m_vfs_fd_foreach(devfs_notify_fd_cb, &ctx);
}

static m_vfs_error_t devfs_fs_mount(m_vfs_mount_t *mount,
                                     const char *source,
                                     void *options)
{
    (void)source;
    (void)options;

    if (mount == NULL) {
        return M_VFS_ERR_INVALID_PARAM;
    }

    devfs_mount_data_t *data = pvPortMalloc(sizeof(*data));
    if (data == NULL) {
        return M_VFS_ERR_NO_MEMORY;
    }

    memset(data, 0, sizeof(*data));
    data->mount = mount;
    data->lock = (portMUX_TYPE)portMUX_INITIALIZER_UNLOCKED;
    data->nodes = NULL;
    data->pending_free = false;

    char normalized[M_VFS_PATH_MAX_LEN];
    if (!m_vfs_path_normalize(mount->target, normalized, sizeof(normalized))) {
        vPortFree(data);
        return M_VFS_ERR_INVALID_PATH;
    }

    m_vfs_node_t *root = devfs_node_create(data,
                                           NULL,
                                           normalized,
                                           devfs_basename(normalized),
                                           NULL,
                                           true);
    if (root == NULL) {
        vPortFree(data);
        return M_VFS_ERR_NO_MEMORY;
    }

    data->root = root;
    mount->root = root;
    mount->fs_private = data;
    devfs_register_mount(data);
    return M_VFS_ERR_OK;
}

static m_vfs_error_t devfs_fs_unmount(m_vfs_mount_t *mount)
{
    if (mount == NULL) {
        return M_VFS_ERR_INVALID_PARAM;
    }

    devfs_mount_data_t *data = (devfs_mount_data_t *)mount->fs_private;
    if (data == NULL) {
        return M_VFS_ERR_INVALID_PARAM;
    }

    devfs_unregister_mount(data);
    data->pending_free = true;
    mount->fs_private = data;
    return M_VFS_ERR_OK;
}

static m_vfs_error_t devfs_fs_lookup(m_vfs_mount_t *mount,
                                     m_vfs_node_t *parent,
                                     const char *name,
                                     m_vfs_node_t **out_node)
{
    if (mount == NULL || parent == NULL || name == NULL || out_node == NULL) {
        return M_VFS_ERR_INVALID_PARAM;
    }

    devfs_mount_data_t *mount_data = (devfs_mount_data_t *)mount->fs_private;
    if (mount_data == NULL || parent->fs_private == NULL) {
        return M_VFS_ERR_NOT_SUPPORTED;
    }

    devfs_node_data_t *parent_data = (devfs_node_data_t *)parent->fs_private;
    if (!parent_data->is_directory) {
        return M_VFS_ERR_NOT_SUPPORTED;
    }

    char child_path[M_VFS_PATH_MAX_LEN];
    if (!devfs_join_path(parent_data->path, name, child_path, sizeof(child_path))) {
        return M_VFS_ERR_INVALID_PATH;
    }

    portENTER_CRITICAL(&s_devfs_lock);
    portENTER_CRITICAL(&mount_data->lock);
    devfs_device_node_t *existing =
            devfs_mount_node_find(mount_data, child_path);
    if (existing != NULL) {
        devfs_node_data_t *existing_data =
                (devfs_node_data_t *)existing->node->fs_private;
        bool stale = (existing_data == NULL) ||
                     (!existing_data->is_directory &&
                      existing_data->entry == NULL);
        if (!stale) {
            m_vfs_node_acquire(existing->node);
            portEXIT_CRITICAL(&mount_data->lock);
            portEXIT_CRITICAL(&s_devfs_lock);
            *out_node = existing->node;
            return M_VFS_ERR_OK;
        }
    }

    devfs_entry_t *entry = devfs_entry_find_locked(child_path);
    bool is_directory = devfs_has_children_locked(child_path);
    if (entry == NULL && !is_directory) {
        portEXIT_CRITICAL(&mount_data->lock);
        portEXIT_CRITICAL(&s_devfs_lock);
        return M_VFS_ERR_NOT_FOUND;
    }

    if (entry != NULL) {
        is_directory = false;
    }

    m_vfs_node_t *node = devfs_node_create(mount_data,
                                           parent,
                                           child_path,
                                           name,
                                           entry,
                                           is_directory);
    devfs_device_node_t *created = NULL;
    if (node != NULL) {
        devfs_node_data_t *node_data = (devfs_node_data_t *)node->fs_private;
        created = (node_data != NULL) ? node_data->device : NULL;
    }
    void (*attach)(const devfs_entry_t *, devfs_device_node_t *) = NULL;
    if (entry != NULL) {
        attach = entry->node_attach;
    }

    if (node == NULL) {
        portEXIT_CRITICAL(&mount_data->lock);
        portEXIT_CRITICAL(&s_devfs_lock);
        return M_VFS_ERR_NO_MEMORY;
    }

    portEXIT_CRITICAL(&mount_data->lock);
    portEXIT_CRITICAL(&s_devfs_lock);

    if (attach != NULL && created != NULL) {
        attach(entry, created);
    }

    *out_node = node;
    return M_VFS_ERR_OK;
}

static m_vfs_error_t devfs_fs_readdir(m_vfs_file_t *file,
                                      m_vfs_dirent_t *entries,
                                      size_t capacity,
                                      size_t *populated)
{
    if (file == NULL || entries == NULL || populated == NULL) {
        return M_VFS_ERR_INVALID_PARAM;
    }

    if (file->node == NULL || file->node->fs_private == NULL) {
        return M_VFS_ERR_NOT_SUPPORTED;
    }

    devfs_node_data_t *data = (devfs_node_data_t *)file->node->fs_private;
    if (!data->is_directory) {
        return M_VFS_ERR_NOT_SUPPORTED;
    }

    devfs_child_t children[DEVFS_CHILD_CAPACITY];
    portENTER_CRITICAL(&s_devfs_lock);
    size_t total = devfs_collect_children_locked(data->path,
                                                children,
                                                DEVFS_CHILD_CAPACITY);
    portEXIT_CRITICAL(&s_devfs_lock);

    size_t offset = file->offset;
    size_t returned = 0;
    for (size_t idx = offset; idx < total && returned < capacity; ++idx) {
        m_vfs_dirent_t *out = &entries[returned];
        out->node = NULL;
        strncpy(out->name, children[idx].name, M_VFS_NAME_MAX_LEN);
        out->name[M_VFS_NAME_MAX_LEN - 1] = '\0';
        out->type = children[idx].is_directory ?
                M_VFS_NODE_TYPE_DIRECTORY :
                M_VFS_NODE_TYPE_DEVICE;
        ++returned;
    }

    file->offset = offset + returned;
    *populated = returned;
    return M_VFS_ERR_OK;
}

static m_vfs_error_t
devfs_fs_open(m_vfs_node_t *node,
              int flags,
              m_vfs_file_t **out_file)
{
    (void)flags;
    if (node == NULL || out_file == NULL) {
        return M_VFS_ERR_INVALID_PARAM;
    }

    devfs_node_data_t *data = (devfs_node_data_t *)node->fs_private;
    if (data == NULL) {
        return M_VFS_ERR_NOT_SUPPORTED;
    }

    m_vfs_file_t *file = m_vfs_file_create(node);
    if (file == NULL) {
        return M_VFS_ERR_NO_MEMORY;
    }

    if (data->is_directory) {
        *out_file = file;
        return M_VFS_ERR_OK;
    }

    if (data->entry == NULL) {
        m_vfs_file_release(file);
        return M_VFS_ERR_NOT_FOUND;
    }

    if (data->entry->ops != NULL && data->entry->ops->open != NULL) {
        m_vfs_error_t err = data->entry->ops->open(data->entry->private_data);
        if (err != M_VFS_ERR_OK) {
            m_vfs_file_release(file);
            return err;
        }
    }

    *out_file = file;
    return M_VFS_ERR_OK;
}

static m_vfs_error_t devfs_fs_close(m_vfs_file_t *file)
{
    if (file == NULL || file->node == NULL || file->node->fs_private == NULL) {
        return M_VFS_ERR_INVALID_PARAM;
    }

    devfs_node_data_t *data = (devfs_node_data_t *)file->node->fs_private;
    if (data == NULL || data->entry == NULL ||
            data->entry->ops == NULL ||
            data->entry->ops->close == NULL) {
        return M_VFS_ERR_OK;
    }

    return data->entry->ops->close(data->entry->private_data);
}

static m_vfs_error_t devfs_fs_read(m_vfs_file_t *file,
                                   void *buffer,
                                   size_t size,
                                   size_t *read)
{
    if (file == NULL || buffer == NULL || read == NULL ||
            file->node == NULL || file->node->fs_private == NULL) {
        return M_VFS_ERR_INVALID_PARAM;
    }

    devfs_node_data_t *data = (devfs_node_data_t *)file->node->fs_private;
    if (data == NULL || data->entry == NULL ||
            data->entry->ops == NULL ||
            data->entry->ops->read == NULL) {
        return (data != NULL && data->entry == NULL) ? M_VFS_ERR_DESTROYED :
               M_VFS_ERR_NOT_SUPPORTED;
    }

    return data->entry->ops->read(data->entry->private_data,
                                  buffer,
                                  size,
                                  read);
}

static m_vfs_error_t devfs_fs_write(m_vfs_file_t *file,
                                    const void *buffer,
                                    size_t size,
                                    size_t *written)
{
    if (file == NULL || buffer == NULL || written == NULL ||
            file->node == NULL || file->node->fs_private == NULL) {
        return M_VFS_ERR_INVALID_PARAM;
    }

    devfs_node_data_t *data = (devfs_node_data_t *)file->node->fs_private;
    if (data == NULL || data->entry == NULL ||
            data->entry->ops == NULL ||
            data->entry->ops->write == NULL) {
        return (data != NULL && data->entry == NULL) ? M_VFS_ERR_DESTROYED :
               M_VFS_ERR_NOT_SUPPORTED;
    }

    return data->entry->ops->write(data->entry->private_data,
                                   buffer,
                                   size,
                                   written);
}

static m_vfs_error_t devfs_fs_ioctl(m_vfs_file_t *file,
                                    unsigned long request,
                                    void *arg)
{
    if (file == NULL || file->node == NULL || file->node->fs_private == NULL) {
        return M_VFS_ERR_INVALID_PARAM;
    }

    devfs_node_data_t *data = (devfs_node_data_t *)file->node->fs_private;
    if (data == NULL) {
        return M_VFS_ERR_INVALID_PARAM;
    }

    if (request == DEVFS_IOCTL_POLL_MASK) {
        devfs_event_mask_t *mask = (devfs_event_mask_t *)arg;
        if (mask == NULL) {
            return M_VFS_ERR_INVALID_PARAM;
        }
        if (data->entry != NULL && data->entry->ops != NULL &&
                data->entry->ops->poll != NULL) {
            *mask = data->entry->ops->poll(data->entry->private_data);
        } else {
            *mask = devfs_event_mask(file->node);
        }
        return M_VFS_ERR_OK;
    }

    if (request == DEVFS_IOCTL_FLUSH) {
        if (data->entry != NULL && data->entry->ops != NULL &&
                data->entry->ops->flush != NULL) {
            return data->entry->ops->flush(data->entry->private_data);
        }
        return M_VFS_ERR_OK;
    }

    if (request == DEVFS_IOCTL_RESET) {
        if (data->entry != NULL && data->entry->ops != NULL &&
                data->entry->ops->reset != NULL) {
            return data->entry->ops->reset(data->entry->private_data);
        }
        return M_VFS_ERR_NOT_SUPPORTED;
    }

    if (request == DEVFS_IOCTL_GET_INFO) {
        if (data->entry != NULL && data->entry->ops != NULL &&
                data->entry->ops->get_info != NULL) {
            return data->entry->ops->get_info(data->entry->private_data,
                                              (devfs_device_info_t *)arg);
        }
        return M_VFS_ERR_NOT_SUPPORTED;
    }

    if (request == DEVFS_IOCTL_DESTROY) {
        if (data->entry != NULL && data->entry->ops != NULL &&
                data->entry->ops->destroy != NULL) {
            data->entry->ops->destroy(data->entry->private_data);
        }
        return M_VFS_ERR_OK;
    }

    if (data->entry != NULL && data->entry->ops != NULL &&
            data->entry->ops->ioctl != NULL) {
        return data->entry->ops->ioctl(data->entry->private_data,
                                       request,
                                       arg);
    }

    return M_VFS_ERR_NOT_SUPPORTED;
}

static m_vfs_error_t devfs_fs_getattr(m_vfs_node_t *node, m_vfs_stat_t *stat)
{
    if (node == NULL || stat == NULL || node->fs_private == NULL) {
        return M_VFS_ERR_INVALID_PARAM;
    }

    devfs_node_data_t *data = (devfs_node_data_t *)node->fs_private;
    if (data == NULL) {
        return M_VFS_ERR_INVALID_PARAM;
    }

    stat->type = data->is_directory ? M_VFS_NODE_TYPE_DIRECTORY :
                                     M_VFS_NODE_TYPE_DEVICE;
    stat->mode = data->is_directory ? M_VFS_DIRECTORY_MODE_DEFAULT :
                                     M_VFS_FILE_MODE_DEFAULT;
    stat->size = 0;
    stat->mtime = 0;
    stat->atime = 0;
    stat->flags = 0;
    return M_VFS_ERR_OK;
}

static m_vfs_error_t devfs_fs_setattr(m_vfs_node_t *node, const m_vfs_stat_t *stat)
{
    (void)node;
    (void)stat;
    return M_VFS_ERR_NOT_SUPPORTED;
}

static void devfs_fs_file_destroy(m_vfs_file_t *file)
{
    (void)file;
}

static const struct m_vfs_fs_ops s_devfs_ops = {
    .mount = devfs_fs_mount,
    .unmount = devfs_fs_unmount,
    .lookup = devfs_fs_lookup,
    .readdir = devfs_fs_readdir,
    .open = devfs_fs_open,
    .close = devfs_fs_close,
    .read = devfs_fs_read,
    .write = devfs_fs_write,
    .ioctl = devfs_fs_ioctl,
    .getattr = devfs_fs_getattr,
    .setattr = devfs_fs_setattr,
    .node_destroy = devfs_node_destroy,
    .file_destroy = devfs_fs_file_destroy,
};

static const m_vfs_fs_type_t s_devfs_fs_type = {
    .name = "devfs",
    .ops = &s_devfs_ops,
    .cookie = NULL,
};

const m_vfs_fs_type_t *m_devfs_fs_type(void)
{
    return &s_devfs_fs_type;
}

static m_vfs_error_t devfs_default_read_zero(void *private_data,
                                             void *buffer,
                                             size_t size,
                                             size_t *read)
{
    (void)private_data;
    if (buffer == NULL || read == NULL) {
        return M_VFS_ERR_INVALID_PARAM;
    }
    memset(buffer, 0, size);
    *read = size;
    return M_VFS_ERR_OK;
}

static m_vfs_error_t devfs_null_read(void *private_data,
                                     void *buffer,
                                     size_t size,
                                     size_t *read)
{
    (void)private_data;
    (void)buffer;
    (void)size;
    if (read == NULL) {
        return M_VFS_ERR_INVALID_PARAM;
    }
    *read = 0;
    return M_VFS_ERR_OK;
}

static m_vfs_error_t devfs_random_read(void *private_data,
                                       void *buffer,
                                       size_t size,
                                       size_t *read)
{
    (void)private_data;
    if (buffer == NULL || read == NULL) {
        return M_VFS_ERR_INVALID_PARAM;
    }

    uint8_t *dst = (uint8_t *)buffer;
    size_t remaining = size;
    while (remaining >= sizeof(uint32_t)) {
        uint32_t value = esp_random();
        memcpy(dst, &value, sizeof(value));
        dst += sizeof(value);
        remaining -= sizeof(value);
    }

    if (remaining > 0) {
        uint32_t value = esp_random();
        memcpy(dst, &value, remaining);
    }

    *read = size;
    return M_VFS_ERR_OK;
}

static m_vfs_error_t devfs_default_write(void *private_data,
                                         const void *buffer,
                                         size_t size,
                                         size_t *written)
{
    (void)private_data;
    (void)buffer;
    if (written == NULL) {
        return M_VFS_ERR_INVALID_PARAM;
    }
    *written = size;
    return M_VFS_ERR_OK;
}

static uint32_t devfs_default_poll(void *private_data)
{
    (void)private_data;
    return DEVFS_EVENT_READABLE | DEVFS_EVENT_WRITABLE;
}

static const devfs_ops_t s_devfs_null_ops = {
    .read = devfs_null_read,
    .write = devfs_default_write,
    .poll = devfs_default_poll,
};

static const devfs_ops_t s_devfs_zero_ops = {
    .read = devfs_default_read_zero,
    .write = devfs_default_write,
    .poll = devfs_default_poll,
};

static const devfs_ops_t s_devfs_random_ops = {
    .read = devfs_random_read,
    .write = devfs_default_write,
    .poll = devfs_default_poll,
};

void m_devfs_register_default_devices(void)
{
    devfs_register("/dev/null", &s_devfs_null_ops, NULL);
    devfs_register("/dev/zero", &s_devfs_zero_ops, NULL);
    devfs_register("/dev/random", &s_devfs_random_ops, NULL);
#if CONFIG_MAGNOLIA_IPC_ENABLED
    devfs_shm_register_devices();
#if CONFIG_MAGNOLIA_DEVFS_PIPES
    devfs_stream_register_pipes();
#endif
#if CONFIG_MAGNOLIA_DEVFS_TTY
    devfs_stream_register_ttys();
#endif
#if CONFIG_MAGNOLIA_DEVFS_PTY
    devfs_stream_register_ptys();
#endif
#endif
}

m_vfs_error_t devfs_register(const char *path,
                             const devfs_ops_t *ops,
                             void *private_data)
{
    return devfs_register_ext(path, ops, private_data, NULL, NULL);
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
    if (path == NULL || ops == NULL) {
        return M_VFS_ERR_INVALID_PARAM;
    }

    char normalized[M_VFS_PATH_MAX_LEN];
    if (!m_vfs_path_normalize(path, normalized, sizeof(normalized))) {
        return M_VFS_ERR_INVALID_PATH;
    }

    portENTER_CRITICAL(&s_devfs_lock);
    if (s_devfs_entry_count >= CONFIG_MAGNOLIA_DEVFS_MAX_DEVICES) {
        portEXIT_CRITICAL(&s_devfs_lock);
        return M_VFS_ERR_TOO_MANY_ENTRIES;
    }

    if (devfs_entry_find_locked(normalized) != NULL) {
        portEXIT_CRITICAL(&s_devfs_lock);
        return M_VFS_ERR_BUSY;
    }

    devfs_entry_t *entry = pvPortMalloc(sizeof(*entry));
    if (entry == NULL) {
        portEXIT_CRITICAL(&s_devfs_lock);
        return M_VFS_ERR_NO_MEMORY;
    }

    memset(entry, 0, sizeof(*entry));
    strncpy(entry->path, normalized, sizeof(entry->path));
    entry->path[sizeof(entry->path) - 1] = '\0';
    strncpy(entry->name, devfs_basename(normalized), sizeof(entry->name));
    entry->name[sizeof(entry->name) - 1] = '\0';
    entry->ops = ops;
    entry->private_data = private_data;
    entry->registered = true;
    entry->node_attach = node_attach;
    entry->node_detach = node_detach;
    entry->node_count = 0;
    entry->nodes = NULL;
    entry->next = s_devfs_entries;
    s_devfs_entries = entry;
    ++s_devfs_entry_count;
    portEXIT_CRITICAL(&s_devfs_lock);
    return M_VFS_ERR_OK;
}

m_vfs_error_t devfs_unregister(const char *path)
{
    if (path == NULL) {
        return M_VFS_ERR_INVALID_PARAM;
    }

    char normalized[M_VFS_PATH_MAX_LEN];
    if (!m_vfs_path_normalize(path, normalized, sizeof(normalized))) {
        return M_VFS_ERR_INVALID_PATH;
    }

    portENTER_CRITICAL(&s_devfs_lock);
    devfs_entry_t **slot = &s_devfs_entries;
    devfs_entry_t *entry = NULL;
    while (*slot != NULL) {
        if (strcmp((*slot)->path, normalized) == 0) {
            entry = *slot;
            *slot = entry->next;
            entry->next = NULL;
            break;
        }
        slot = &(*slot)->next;
    }

    if (entry == NULL) {
        portEXIT_CRITICAL(&s_devfs_lock);
        return M_VFS_ERR_NOT_FOUND;
    }

    --s_devfs_entry_count;
    devfs_device_node_t *device = entry->nodes;
    entry->nodes = NULL;
    entry->registered = false;
    portEXIT_CRITICAL(&s_devfs_lock);

    const devfs_event_mask_t notify_mask = DEVFS_EVENT_ERROR | DEVFS_EVENT_HANGUP;
    while (device != NULL) {
        devfs_device_node_t *next = device->next_entry;
        if (device->node != NULL && device->node->fs_private != NULL) {
            devfs_node_data_t *data = (devfs_node_data_t *)device->node->fs_private;
            if (data != NULL) {
                devfs_notify(device->node, notify_mask);
                data->entry = NULL;
            }
        }
        if (entry->node_detach != NULL) {
            entry->node_detach(entry, device);
        }
        device = next;
    }

    atomic_fetch_add_explicit(&s_devfs_unregister_events,
                              1,
                              memory_order_relaxed);
    vPortFree(entry);
    return M_VFS_ERR_OK;
}

typedef struct {
    const m_vfs_node_t *node;
    size_t *count;
} devfs_waiter_count_ctx_t;

static bool devfs_waiter_count_cb(m_job_id_t job,
                                  int fd,
                                  const m_vfs_file_t *file,
                                  void *user_data)
{
    (void)job;
    (void)fd;

    if (user_data == NULL || file == NULL || file->node == NULL) {
        return true;
    }

    devfs_waiter_count_ctx_t *ctx = (devfs_waiter_count_ctx_t *)user_data;
    if (file->node == ctx->node) {
        *ctx->count += file->waiters.count;
    }
    return true;
}

void devfs_diag_device_iterate(devfs_diag_device_iter_fn cb, void *user_data)
{
    if (cb == NULL) {
        return;
    }

    portENTER_CRITICAL(&s_devfs_lock);
    devfs_entry_t *entry = s_devfs_entries;
    while (entry != NULL) {
        devfs_device_info_t info = {0};
        strncpy(info.path, entry->path, sizeof(info.path));
        strncpy(info.name, entry->name, sizeof(info.name));
        info.path[sizeof(info.path) - 1] = '\0';
        info.name[sizeof(info.name) - 1] = '\0';
        if (entry->nodes != NULL) {
            devfs_device_node_t *device = entry->nodes;
            info.ready_mask = devfs_event_mask(device->node);
            info.notify_count = device->notify_count;
            info.poll_count = device->poll_count;
            info.blocked_count = device->blocked_count;
            info.waiter_count = 0;
            info.shm_used = 0;
            info.shm_capacity = 0;
            info.unregister_events = s_devfs_unregister_events;
        }
        portEXIT_CRITICAL(&s_devfs_lock);
        if (!cb(&info, user_data)) {
            return;
        }
        portENTER_CRITICAL(&s_devfs_lock);
        entry = entry->next;
    }
    portEXIT_CRITICAL(&s_devfs_lock);
}

void devfs_diag_tree_snapshot(devfs_diag_tree_iter_fn cb, void *user_data)
{
    if (cb == NULL) {
        return;
    }

    size_t capacity = CONFIG_MAGNOLIA_DEVFS_MAX_DEVICES + 16;
    m_vfs_node_t **snapshot = pvPortMalloc(sizeof(m_vfs_node_t *) * capacity);
    if (snapshot == NULL) {
        return;
    }

    size_t count = 0;
    portENTER_CRITICAL(&s_devfs_lock);
    devfs_mount_data_t *mount = s_devfs_mounts;
    while (mount != NULL) {
        devfs_device_node_t *device = mount->nodes;
        while (device != NULL && count < capacity) {
            if (device->node != NULL) {
                snapshot[count++] = device->node;
            }
            device = device->next_mount;
        }
        mount = mount->next;
    }
    portEXIT_CRITICAL(&s_devfs_lock);

    for (size_t i = 0; i < count; ++i) {
        if (!cb(snapshot[i], user_data)) {
            break;
        }
    }
    vPortFree(snapshot);
}

void devfs_diag_waiters(devfs_diag_waiter_iter_fn cb, void *user_data)
{
    if (cb == NULL) {
        return;
    }

    portENTER_CRITICAL(&s_devfs_lock);
    devfs_mount_data_t *mount = s_devfs_mounts;
    while (mount != NULL) {
        devfs_device_node_t *device = mount->nodes;
        while (device != NULL) {
            size_t count = 0;
            devfs_waiter_count_ctx_t ctx = {
                .node = device->node,
                .count = &count,
            };
            m_vfs_fd_foreach(devfs_waiter_count_cb, &ctx);
            if (count > 0) {
                devfs_diag_waiter_info_t info = {0};
                info.waiter_count = count;
                info.ready_mask = devfs_event_mask(device->node);
                if (device->node != NULL &&
                        device->node->fs_private != NULL) {
                    devfs_node_data_t *data =
                            (devfs_node_data_t *)device->node->fs_private;
                    strncpy(info.path,
                            data->path,
                            sizeof(info.path));
                    info.path[sizeof(info.path) - 1] = '\0';
                }
                portEXIT_CRITICAL(&s_devfs_lock);
                if (!cb(&info, user_data)) {
                    return;
                }
                portENTER_CRITICAL(&s_devfs_lock);
            }
            device = device->next_mount;
        }
        mount = mount->next;
    }
    portEXIT_CRITICAL(&s_devfs_lock);
}

#if CONFIG_MAGNOLIA_IPC_ENABLED
void devfs_diag_shm_info(devfs_diag_shm_iter_fn cb, void *user_data)
{
    if (cb == NULL) {
        return;
    }

    for (size_t i = 0; i < DEVFS_SHM_DEVICE_COUNT; ++i) {
        devfs_shm_device_context_t *ctx = &g_devfs_shm_contexts[i];
        devfs_diag_shm_info_t info = {0};
        info.path = ctx->path;
        info.ready_mask = ctx->ready_mask;
        if (ctx->handle != IPC_HANDLE_INVALID) {
            ipc_shm_info_t shm_info = {0};
            if (ipc_shm_query(ctx->handle, &shm_info) == IPC_OK) {
                info.used = shm_info.ring_used;
                info.capacity = shm_info.ring_capacity;
            }
        }
        if (!cb(&info, user_data)) {
            return;
        }
    }
}
#else
void devfs_diag_shm_info(devfs_diag_shm_iter_fn cb, void *user_data)
{
    (void)cb;
    (void)user_data;
}
#endif

size_t devfs_diag_unregister_events(void)
{
    return atomic_load_explicit(&s_devfs_unregister_events,
                                memory_order_relaxed);
}

size_t devfs_diag_total_poll_count(void)
{
    return atomic_load_explicit(&s_devfs_poll_total,
                                memory_order_relaxed);
}

#endif /* CONFIG_MAGNOLIA_VFS_DEVFS */
