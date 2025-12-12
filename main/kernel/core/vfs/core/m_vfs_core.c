#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <inttypes.h>

#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"

#include "sdkconfig.h"
#include "esp_log.h"
#include "esp_partition.h"
#include "kernel/core/job/m_job_core.h"
#include "kernel/core/vfs/core/m_vfs_jobcwd.h"
#include "kernel/core/ipc/ipc_scheduler_bridge.h"
#include "kernel/core/timer/m_timer.h"
#include "kernel/core/vfs/core/m_vfs_object.h"
#include "kernel/core/vfs/core/m_vfs_registry.h"
#include "kernel/core/vfs/core/m_vfs_test.h"
#include "kernel/core/vfs/cache/m_vfs_read_cache.h"
#include "kernel/core/vfs/core/m_vfs_wait.h"
#include "kernel/core/vfs/core/m_vfs_errno.h"
#include "kernel/core/vfs/fd/m_vfs_fd.h"
#include "kernel/core/vfs/core/m_vfs_selftests.h"
#include "kernel/core/vfs/m_vfs.h"
#include "../path/m_vfs_path.h"
#include "kernel/core/vfs/ramfs/ramfs.h"
#if CONFIG_MAGNOLIA_VFS_DEVFS
#include "kernel/vfs/fs/devfs/devfs.h"
#include "kernel/vfs/fs/devfs/devfs_internal.h"
#endif
#if CONFIG_MAGNOLIA_LITTLEFS_ENABLED
#include "kernel/vfs/fs/littlefs/littlefs_fs.h"
#endif

static bool g_vfs_initialized;

static void
_m_vfs_log_partitions(void)
{
    static const char *TAG = "vfs_partitions";

    esp_partition_iterator_t it = esp_partition_find(ESP_PARTITION_TYPE_ANY,
                                                     ESP_PARTITION_SUBTYPE_ANY,
                                                     NULL);
    if (it == NULL) {
        ESP_LOGW(TAG, "no partitions found");
        return;
    }

    ESP_LOGI(TAG, "available partitions:");
    while (it != NULL) {
        const esp_partition_t *p = esp_partition_get(it);
        if (p != NULL) {
            ESP_LOGI(TAG,
                     "label=%s type=0x%02x subtype=0x%02x addr=0x%08"PRIx32" size=%"PRIu32" erase=%"PRIu32,
                     p->label, p->type, p->subtype, p->address, p->size, p->erase_size);
        }
        it = esp_partition_next(it);
    }
    esp_partition_iterator_release(it);
}

static bool
_m_vfs_should_inject(m_vfs_error_t *out)
{
    if (!m_vfs_test_error_injection_enabled()) {
        return false;
    }

    if (out != NULL) {
        *out = m_vfs_test_error_injection_code();
    }
    return true;
}

static inline m_vfs_error_t
_m_vfs_record_result(m_vfs_error_t err)
{
    return m_vfs_record_error(err);
}

static m_vfs_error_t
_m_vfs_wait_result_to_error(ipc_wait_result_t result)
{
    switch (result) {
    case IPC_WAIT_RESULT_OK:
        return _m_vfs_record_result(M_VFS_ERR_OK);
    case IPC_WAIT_RESULT_TIMEOUT:
        return _m_vfs_record_result(M_VFS_ERR_TIMEOUT);
    case IPC_WAIT_RESULT_OBJECT_DESTROYED:
        return _m_vfs_record_result(M_VFS_ERR_DESTROYED);
    case IPC_WAIT_RESULT_DEVICE_REMOVED:
        return _m_vfs_record_result(M_VFS_ERR_DESTROYED);
    default:
        return _m_vfs_record_result(M_VFS_ERR_INTERRUPTED);
    }
}

static bool
_m_vfs_copy_segment(const m_vfs_path_segment_t *segment,
                    char *out,
                    size_t capacity)
{
    if (segment == NULL || out == NULL || capacity == 0 ||
            segment->length >= capacity) {
        return false;
    }

    memcpy(out, segment->name, segment->length);
    out[segment->length] = '\0';
    return true;
}

typedef struct {
    const m_vfs_mount_t *mount;
    bool found;
} _m_vfs_mount_node_ctx_t;

static bool
_m_vfs_mount_node_cb(const m_vfs_node_t *node, void *user_data)
{
    if (node == NULL || user_data == NULL) {
        return true;
    }

    _m_vfs_mount_node_ctx_t *ctx = user_data;
    if (node->destroyed || node->mount == NULL) {
        return true;
    }

    if (node->mount == ctx->mount) {
        ctx->found = true;
        return false;
    }

    return true;
}

static bool
_m_vfs_mount_has_active_nodes(const m_vfs_mount_t *mount)
{
    if (mount == NULL) {
        return false;
    }

    _m_vfs_mount_node_ctx_t ctx = {
        .mount = mount,
        .found = false,
    };
    m_vfs_node_iterate(_m_vfs_mount_node_cb, &ctx);
    return ctx.found;
}

static bool
_m_vfs_path_parent(const m_vfs_path_t *path,
                    m_vfs_path_t *parent,
                    m_vfs_path_segment_t *leaf)
{
    if (path == NULL || path->segment_count == 0) {
        return false;
    }

    if (leaf != NULL) {
        *leaf = path->segments[path->segment_count - 1];
    }

    if (parent == NULL) {
        return true;
    }

    const char *segment_start = path->segments[path->segment_count - 1].name;
    const char *slash = segment_start;
    while (slash > path->normalized && slash[-1] != '/') {
        --slash;
    }

    size_t parent_length = (size_t)(slash - path->normalized);
    if (parent_length == 0) {
        strncpy(parent->normalized, "/", sizeof(parent->normalized));
        parent->normalized[1] = '\0';
    } else {
        if (parent_length >= sizeof(parent->normalized)) {
            return false;
        }
        memcpy(parent->normalized, path->normalized, parent_length);
        parent->normalized[parent_length] = '\0';
    }

    return m_vfs_path_parse(parent->normalized, parent);
}

static m_vfs_error_t
_m_vfs_resolve_parent(m_job_id_t job,
                       const m_vfs_path_t *path,
                       m_vfs_node_t **out_parent,
                       char *leaf_name,
                       size_t name_capacity)
{
    if (path == NULL || out_parent == NULL || leaf_name == NULL) {
        return M_VFS_ERR_INVALID_PARAM;
    }

    m_vfs_path_t parent_path;
    m_vfs_path_segment_t leaf_segment;
    if (!_m_vfs_path_parent(path, &parent_path, &leaf_segment)) {
        return M_VFS_ERR_INVALID_PATH;
    }

    if (!_m_vfs_copy_segment(&leaf_segment, leaf_name, name_capacity)) {
        return M_VFS_ERR_INVALID_PATH;
    }

    m_vfs_node_t *parent = NULL;
    m_vfs_error_t err = m_vfs_path_resolve(job, &parent_path, &parent);
    if (err != M_VFS_ERR_OK) {
        return err;
    }

    *out_parent = parent;
    return M_VFS_ERR_OK;
}

static m_vfs_error_t
_m_vfs_job_error_to_vfs(job_ctx_error_t err)
{
    switch (err) {
    case JOB_CTX_OK:
        return M_VFS_ERR_OK;
    case JOB_CTX_ERR_INVALID_PARAM:
    case JOB_CTX_ERR_INVALID_FIELD:
        return M_VFS_ERR_INVALID_PARAM;
    case JOB_CTX_ERR_BUFFER_TOO_SMALL:
        return M_VFS_ERR_INVALID_PATH;
    case JOB_CTX_ERR_NO_PERMISSION:
        return M_VFS_ERR_BUSY;
    default:
        return M_VFS_ERR_BUSY;
    }
}

static bool
_m_vfs_build_absolute_path(m_job_id_t job,
                           const char *path,
                           char *out,
                           size_t capacity)
{
    if (path == NULL || out == NULL || capacity == 0) {
        return false;
    }

    if (path[0] == '/') {
        return m_vfs_path_normalize(path, out, capacity);
    }

    if (job == NULL) {
        return false;
    }

    char cwd[JOB_CTX_CWD_MAX_LEN] = {0};
    if (m_job_field_get(job, JOB_CTX_FIELD_CWD, cwd, sizeof(cwd)) != JOB_CTX_OK) {
        return false;
    }
    m_vfs_job_cwd_update(job, cwd);

    char combined[M_VFS_PATH_MAX_LEN] = {0};
    if (cwd[0] == '\0') {
        cwd[0] = '/';
        cwd[1] = '\0';
    }

    int needed = 0;
    if (cwd[1] == '\0') {
        needed = snprintf(combined, sizeof(combined), "/%s", path);
    } else {
        needed = snprintf(combined, sizeof(combined), "%s/%s", cwd, path);
    }
    if (needed < 0 || (size_t)needed >= sizeof(combined)) {
        return false;
    }

    return m_vfs_path_normalize(combined, out, capacity);
}

static m_vfs_error_t
_m_vfs_parse_user_path(m_job_id_t job,
                       const char *path,
                       m_vfs_path_t *result)
{
    if (path == NULL || result == NULL) {
        return M_VFS_ERR_INVALID_PARAM;
    }

    char normalized[M_VFS_PATH_MAX_LEN] = {0};
    if (!_m_vfs_build_absolute_path(job, path, normalized, sizeof(normalized))) {
        return M_VFS_ERR_INVALID_PATH;
    }

    if (!m_vfs_path_parse(normalized, result)) {
        return M_VFS_ERR_INVALID_PATH;
    }

    return M_VFS_ERR_OK;
}

static m_vfs_error_t
_m_vfs_mount_setup(const char *target,
                    const m_vfs_fs_type_t *type,
                    void *options)
{
    if (target == NULL || type == NULL) {
        return M_VFS_ERR_INVALID_PARAM;
    }

    char normalized[M_VFS_PATH_MAX_LEN];
    if (!m_vfs_path_normalize(target, normalized, sizeof(normalized))) {
        return M_VFS_ERR_INVALID_PATH;
    }

    m_vfs_mount_t *mount = pvPortMalloc(sizeof(*mount));
    if (mount == NULL) {
        return M_VFS_ERR_NO_MEMORY;
    }

    mount->fs_type = type;
    mount->root = NULL;
    mount->fs_private = NULL;
    strncpy(mount->target, normalized, M_VFS_PATH_MAX_LEN);
    mount->target[M_VFS_PATH_MAX_LEN - 1] = '\0';
    mount->lock = (portMUX_TYPE)portMUX_INITIALIZER_UNLOCKED;
    mount->active = false;
    mount->refcount = 1;
    mount->next = NULL;
    mount->target_len = strnlen(mount->target, M_VFS_PATH_MAX_LEN);
    mount->sequence = 0;
    mount->registry_index = SIZE_MAX;

    if (type->ops == NULL || type->ops->mount == NULL) {
        vPortFree(mount);
        return M_VFS_ERR_NOT_SUPPORTED;
    }

    m_vfs_error_t err = type->ops->mount(mount, normalized, options);
    if (err != M_VFS_ERR_OK) {
        vPortFree(mount);
        return err;
    }

    err = m_vfs_registry_mount_add(mount);
    if (err != M_VFS_ERR_OK) {
        if (type->ops->unmount != NULL) {
            type->ops->unmount(mount);
        }
        vPortFree(mount);
        return err;
    }

    mount->active = true;
    return M_VFS_ERR_OK;
}

m_vfs_error_t
m_vfs_init(void)
{
    if (g_vfs_initialized) {
        return M_VFS_ERR_OK;
    }

    m_vfs_registry_init();
    m_vfs_job_cwd_init();
    m_vfs_fd_init();

    const m_vfs_fs_type_t *ramfs = m_ramfs_fs_type();
    if (ramfs != NULL) {
        m_vfs_fs_type_register(ramfs);
    }
#if CONFIG_MAGNOLIA_VFS_DEVFS
    const m_vfs_fs_type_t *devfs = m_devfs_fs_type();
    if (devfs != NULL) {
        m_vfs_fs_type_register(devfs);
        m_devfs_register_default_devices();
    }
#endif

#if CONFIG_MAGNOLIA_LITTLEFS_ENABLED
    const m_vfs_fs_type_t *littlefs = m_littlefs_fs_type();
    if (littlefs != NULL) {
        m_vfs_fs_type_register(littlefs);
    }
#endif

    _m_vfs_log_partitions();

    g_vfs_initialized = true;
    return M_VFS_ERR_OK;
}

m_vfs_error_t
m_vfs_fs_type_register(const m_vfs_fs_type_t *type)
{
    return m_vfs_registry_fs_type_register(type);
}

m_vfs_error_t
m_vfs_fs_type_unregister(const char *name)
{
    return m_vfs_registry_fs_type_unregister(name);
}

const m_vfs_fs_type_t *
m_vfs_fs_type_find(const char *name)
{
    return m_vfs_registry_fs_type_find(name);
}

m_vfs_error_t
m_vfs_mount(const char *target,
            const char *fs_type,
            void *options)
{
    if (target == NULL || fs_type == NULL) {
        return M_VFS_ERR_INVALID_PARAM;
    }

    const m_vfs_fs_type_t *type = m_vfs_registry_fs_type_find(fs_type);
    if (type == NULL) {
        return M_VFS_ERR_NOT_FOUND;
    }

    return _m_vfs_mount_setup(target, type, options);
}

static m_vfs_error_t
_m_vfs_mount_teardown(m_vfs_mount_t *mount, bool force)
{
    if (mount == NULL) {
        return M_VFS_ERR_INVALID_PARAM;
    }

    if (!force && _m_vfs_mount_has_active_nodes(mount)) {
        return M_VFS_ERR_BUSY;
    }

#if CONFIG_MAGNOLIA_VFS_FORCE_UNMOUNT
    if (force) {
        m_vfs_fd_close_mount_fds(mount);
    }
#else
    if (force) {
        return M_VFS_ERR_NOT_SUPPORTED;
    }
#endif

    mount->active = false;
    m_vfs_registry_mount_remove(mount);

    if (mount->fs_type != NULL &&
            mount->fs_type->ops != NULL &&
            mount->fs_type->ops->unmount != NULL) {
        mount->fs_type->ops->unmount(mount);
    }

    if (mount->root != NULL) {
        m_vfs_node_release(mount->root);
        mount->root = NULL;
    }

    vPortFree(mount);
    return M_VFS_ERR_OK;
}

static m_vfs_error_t
_m_vfs_unmount_impl(const char *target, bool force)
{
    if (target == NULL) {
        return M_VFS_ERR_INVALID_PARAM;
    }

    char normalized[M_VFS_PATH_MAX_LEN];
    if (!m_vfs_path_normalize(target, normalized, sizeof(normalized))) {
        return M_VFS_ERR_INVALID_PATH;
    }

    m_vfs_mount_t *mount = m_vfs_registry_mount_find(normalized);
    if (mount == NULL) {
        return M_VFS_ERR_NOT_FOUND;
    }

    return _m_vfs_mount_teardown(mount, force);
}

m_vfs_error_t
m_vfs_unmount(const char *target)
{
    return _m_vfs_unmount_impl(target, false);
}

#if CONFIG_MAGNOLIA_VFS_FORCE_UNMOUNT
m_vfs_error_t
m_vfs_unmount_force(const char *target)
{
    return _m_vfs_unmount_impl(target, true);
}
#else
m_vfs_error_t
m_vfs_unmount_force(const char *target)
{
    (void)target;
    return M_VFS_ERR_NOT_SUPPORTED;
}
#endif

static m_vfs_error_t
_m_vfs_read_internal(m_job_id_t job,
                     int fd,
                     void *buffer,
                     size_t size,
                     size_t *read,
                     const m_timer_deadline_t *deadline)
{
    if (buffer == NULL || read == NULL) {
        return _m_vfs_record_result(M_VFS_ERR_INVALID_PARAM);
    }

    m_vfs_error_t err;
    if (_m_vfs_should_inject(&err)) {
        return _m_vfs_record_result(err);
    }

    m_vfs_file_t *file = m_vfs_fd_lookup(job, fd);
    if (file == NULL || file->node == NULL || file->node->fs_type == NULL ||
            file->node->fs_type->ops == NULL ||
            file->node->fs_type->ops->read == NULL) {
        return _m_vfs_record_result(M_VFS_ERR_NOT_SUPPORTED);
    }

    size_t total = 0;
    err = M_VFS_ERR_OK;
    while (total < size) {
        size_t remaining = size - total;

        if (m_vfs_read_cache_enabled_for(file)) {
            size_t cached = 0;
            err = m_vfs_read_cache_read(file,
                                        (uint8_t *)buffer + total,
                                        remaining,
                                        &cached,
                                        file->node->fs_type->ops->read);
            if (err == M_VFS_ERR_WOULD_BLOCK) {
                ipc_wait_result_t wait = m_vfs_file_wait(file,
                                                        M_SCHED_WAIT_REASON_SHM_READ,
                                                        deadline);
                if (wait != IPC_WAIT_RESULT_OK) {
                    return _m_vfs_wait_result_to_error(wait);
                }
                continue;
            }
            if (err == M_VFS_ERR_NOT_SUPPORTED) {
                err = M_VFS_ERR_OK;
            } else if (err != M_VFS_ERR_OK) {
                return _m_vfs_record_result(err);
            }
            if (cached > 0) {
                total += cached;
                m_vfs_file_set_offset(file, file->offset + cached);
                continue;
            }
            break;
        }

        size_t bytes = 0;
        while (true) {
            err = file->node->fs_type->ops->read(file,
                                                 (uint8_t *)buffer + total,
                                                 remaining,
                                                 &bytes);
            if (err == M_VFS_ERR_WOULD_BLOCK) {
                ipc_wait_result_t wait = m_vfs_file_wait(file,
                                                        M_SCHED_WAIT_REASON_SHM_READ,
                                                        deadline);
                if (wait != IPC_WAIT_RESULT_OK) {
                    return _m_vfs_wait_result_to_error(wait);
                }
                continue;
            }
            break;
        }

        if (err != M_VFS_ERR_OK) {
            break;
        }
        if (bytes == 0) {
            break;
        }

        total += bytes;
        m_vfs_file_set_offset(file, file->offset + bytes);
    }

    if (err == M_VFS_ERR_OK) {
        *read = total;
    } else {
        *read = 0;
    }

    return _m_vfs_record_result(err);
}

static m_vfs_error_t
_m_vfs_write_internal(m_job_id_t job,
                      int fd,
                      const void *buffer,
                      size_t size,
                      size_t *written,
                      const m_timer_deadline_t *deadline)
{
    if (buffer == NULL || written == NULL) {
        return _m_vfs_record_result(M_VFS_ERR_INVALID_PARAM);
    }

    m_vfs_error_t err;
    if (_m_vfs_should_inject(&err)) {
        return _m_vfs_record_result(err);
    }

    m_vfs_file_t *file = m_vfs_fd_lookup(job, fd);
    if (file == NULL || file->node == NULL || file->node->fs_type == NULL ||
            file->node->fs_type->ops == NULL ||
            file->node->fs_type->ops->write == NULL) {
        return _m_vfs_record_result(M_VFS_ERR_NOT_SUPPORTED);
    }

    m_vfs_read_cache_flush_file(file);

    size_t bytes = 0;
    while (true) {
        err = file->node->fs_type->ops->write(file, buffer, size, &bytes);
        if (err == M_VFS_ERR_WOULD_BLOCK) {
            ipc_wait_result_t wait = m_vfs_file_wait(file,
                                                    M_SCHED_WAIT_REASON_SHM_WRITE,
                                                    deadline);
            if (wait != IPC_WAIT_RESULT_OK) {
                return _m_vfs_wait_result_to_error(wait);
            }
            continue;
        }
        break;
    }

    if (err == M_VFS_ERR_OK) {
        *written = bytes;
        m_vfs_file_set_offset(file, file->offset + bytes);
    }

    return _m_vfs_record_result(err);
}

m_vfs_error_t
m_vfs_open(m_job_id_t job,
           const char *path,
           int flags,
           int *out_fd)
{
    if (path == NULL || out_fd == NULL) {
        return _m_vfs_record_result(M_VFS_ERR_INVALID_PARAM);
    }

    if (_m_vfs_should_inject(out_fd ? NULL : NULL)) {
        return _m_vfs_record_result(M_VFS_ERR_BUSY);
    }

    m_vfs_path_t parsed;
    m_vfs_error_t err = _m_vfs_parse_user_path(job, path, &parsed);
    if (err != M_VFS_ERR_OK) {
        return _m_vfs_record_result(err);
    }

    m_vfs_node_t *node = NULL;
    err = m_vfs_path_resolve(job, &parsed, &node);
    if (err != M_VFS_ERR_OK) {
        return _m_vfs_record_result(err);
    }

    if (node == NULL || node->fs_type == NULL || node->fs_type->ops == NULL ||
            node->fs_type->ops->open == NULL) {
        if (node != NULL) {
            m_vfs_node_release(node);
        }
        return _m_vfs_record_result(M_VFS_ERR_NOT_SUPPORTED);
    }

    m_vfs_file_t *file = NULL;
    err = node->fs_type->ops->open(node, flags, &file);
    m_vfs_node_release(node);
    if (err != M_VFS_ERR_OK || file == NULL) {
        if (file != NULL) {
            m_vfs_file_release(file);
        }
        return _m_vfs_record_result(err);
    }

    int fd = m_vfs_fd_allocate(job, file);
    if (fd < 0) {
        if (node->fs_type->ops->close != NULL) {
            node->fs_type->ops->close(file);
        }
        m_vfs_file_release(file);
        return _m_vfs_record_result(M_VFS_ERR_TOO_MANY_ENTRIES);
    }

    *out_fd = fd;
    return _m_vfs_record_result(M_VFS_ERR_OK);
}

m_vfs_error_t
m_vfs_read(m_job_id_t job,
           int fd,
           void *buffer,
           size_t size,
           size_t *read)
{
    return _m_vfs_read_internal(job, fd, buffer, size, read, NULL);
}

m_vfs_error_t
m_vfs_read_timed(m_job_id_t job,
                 int fd,
                 void *buffer,
                 size_t size,
                 size_t *read,
                 const m_timer_deadline_t *deadline)
{
    return _m_vfs_read_internal(job, fd, buffer, size, read, deadline);
}

m_vfs_error_t
m_vfs_write(m_job_id_t job,
            int fd,
            const void *buffer,
            size_t size,
            size_t *written)
{
    return _m_vfs_write_internal(job, fd, buffer, size, written, NULL);
}

m_vfs_error_t
m_vfs_write_timed(m_job_id_t job,
                  int fd,
                  const void *buffer,
                  size_t size,
           size_t *written,
           const m_timer_deadline_t *deadline)
{
    return _m_vfs_write_internal(job, fd, buffer, size, written, deadline);
}

m_vfs_error_t
m_vfs_dup(m_job_id_t job,
          int oldfd,
          int *out_fd)
{
    if (out_fd == NULL) {
        return _m_vfs_record_result(M_VFS_ERR_INVALID_PARAM);
    }

    m_vfs_file_t *file = m_vfs_fd_lookup(job, oldfd);
    if (file == NULL) {
        return _m_vfs_record_result(M_VFS_ERR_INVALID_PARAM);
    }

    int fd = m_vfs_fd_allocate(job, file);
    if (fd < 0) {
        return _m_vfs_record_result(M_VFS_ERR_TOO_MANY_ENTRIES);
    }

    *out_fd = fd;
    return _m_vfs_record_result(M_VFS_ERR_OK);
}

m_vfs_error_t
m_vfs_dup2(m_job_id_t job,
           int oldfd,
           int newfd)
{
    if (newfd < 0) {
        return _m_vfs_record_result(M_VFS_ERR_INVALID_PARAM);
    }

    if (oldfd == newfd) {
        return _m_vfs_record_result(M_VFS_ERR_OK);
    }

    m_vfs_file_t *file = m_vfs_fd_lookup(job, oldfd);
    if (file == NULL) {
        return _m_vfs_record_result(M_VFS_ERR_INVALID_PARAM);
    }

    m_vfs_fd_release(job, newfd);
    m_vfs_error_t err = m_vfs_fd_assign(job, newfd, file);
    return _m_vfs_record_result(err);
}

#if CONFIG_MAGNOLIA_VFS_DEVFS
static bool
_m_vfs_node_is_devfs(const m_vfs_node_t *node)
{
    const m_vfs_fs_type_t *devfs = m_devfs_fs_type();
    return (node != NULL && node->fs_type == devfs);
}

static uint32_t
_m_vfs_devfs_mask_to_poll(devfs_event_mask_t mask)
{
    uint32_t result = 0;
    if (mask & DEVFS_EVENT_READABLE) {
        result |= M_VFS_POLLIN;
    }
    if (mask & DEVFS_EVENT_WRITABLE) {
        result |= M_VFS_POLLOUT;
    }
    if (mask & DEVFS_EVENT_ERROR) {
        result |= M_VFS_POLLERR;
    }
    if (mask & DEVFS_EVENT_HANGUP) {
        result |= M_VFS_POLLHUP;
    }
    return result;
}
#endif

m_vfs_error_t
m_vfs_poll(m_job_id_t job,
           m_vfs_pollfd_t *fds,
           size_t count,
           const m_timer_deadline_t *deadline,
           size_t *ready)
{
    if (fds == NULL || count == 0) {
        return M_VFS_ERR_INVALID_PARAM;
    }

#if CONFIG_MAGNOLIA_VFS_DEVFS
    size_t ready_count = 0;
    while (true) {
        ready_count = 0;
        m_vfs_file_t *wait_file = NULL;
        for (size_t i = 0; i < count; ++i) {
            m_vfs_pollfd_t *entry = &fds[i];
            entry->revents = 0;

            m_vfs_file_t *file = m_vfs_fd_lookup(job, entry->fd);
            if (file == NULL || file->node == NULL ||
                    !_m_vfs_node_is_devfs(file->node)) {
                entry->revents = M_VFS_POLLERR;
                ++ready_count;
                continue;
            }

            const devfs_entry_t *dev_entry = devfs_entry_from_node(file->node);
            if (dev_entry == NULL || dev_entry->ops == NULL) {
                entry->revents = M_VFS_POLLERR;
                ++ready_count;
                continue;
            }

            uint32_t mask = 0;
            if (dev_entry->ops->poll != NULL) {
                mask = dev_entry->ops->poll(dev_entry->private_data);
            } else {
                mask = devfs_event_mask(file->node);
            }

            devfs_record_poll(file->node);
            uint32_t requested = entry->events;
            if (requested == 0) {
                requested = M_VFS_POLLIN | M_VFS_POLLOUT |
                            M_VFS_POLLERR | M_VFS_POLLHUP;
            }
            uint32_t revents = _m_vfs_devfs_mask_to_poll(mask) & requested;
            entry->revents = revents;
            if (revents != 0) {
                ++ready_count;
            }

            if (wait_file == NULL) {
                wait_file = file;
            }
        }

        if (ready_count > 0) {
            if (ready != NULL) {
                *ready = ready_count;
            }
            return _m_vfs_record_result(M_VFS_ERR_OK);
        }

        if (wait_file == NULL) {
            return _m_vfs_record_result(M_VFS_ERR_INVALID_PARAM);
        }

        ipc_wait_result_t wait = m_vfs_file_wait(wait_file,
                                                M_SCHED_WAIT_REASON_EVENT,
                                                deadline);
        if (wait != IPC_WAIT_RESULT_OK) {
            if (ready != NULL) {
                *ready = 0;
            }
            return _m_vfs_wait_result_to_error(wait);
        }
    }
#else
    (void)job;
    (void)fds;
    (void)count;
    (void)deadline;
    (void)ready;
    return _m_vfs_record_result(M_VFS_ERR_NOT_SUPPORTED);
#endif
}

m_vfs_error_t
m_vfs_readdir(m_job_id_t job,
              int fd,
              m_vfs_dirent_t *entries,
              size_t capacity,
              size_t *populated)
{
    if (entries == NULL || populated == NULL) {
        return _m_vfs_record_result(M_VFS_ERR_INVALID_PARAM);
    }

    if (_m_vfs_should_inject(NULL)) {
        return _m_vfs_record_result(M_VFS_ERR_BUSY);
    }

    m_vfs_file_t *file = m_vfs_fd_lookup(job, fd);
    if (file == NULL || file->node == NULL || file->node->fs_type == NULL ||
            file->node->fs_type->ops == NULL ||
            file->node->fs_type->ops->readdir == NULL) {
        return _m_vfs_record_result(M_VFS_ERR_NOT_SUPPORTED);
    }

    m_vfs_error_t result = file->node->fs_type->ops->readdir(file,
                                                             entries,
                                                             capacity,
                                                             populated);
    return _m_vfs_record_result(result);
}

m_vfs_error_t
m_vfs_ioctl(m_job_id_t job,
            int fd,
            unsigned long request,
            void *arg)
{
    if (_m_vfs_should_inject(NULL)) {
        return _m_vfs_record_result(M_VFS_ERR_BUSY);
    }

    m_vfs_file_t *file = m_vfs_fd_lookup(job, fd);
    if (file == NULL || file->node == NULL || file->node->fs_type == NULL ||
            file->node->fs_type->ops == NULL ||
            file->node->fs_type->ops->ioctl == NULL) {
        return _m_vfs_record_result(M_VFS_ERR_NOT_SUPPORTED);
    }

    m_vfs_error_t result = file->node->fs_type->ops->ioctl(file, request, arg);
    return _m_vfs_record_result(result);
}

m_vfs_error_t
m_vfs_close(m_job_id_t job,
            int fd)
{
    m_vfs_file_t *file = m_vfs_fd_lookup(job, fd);
    if (file == NULL) {
        return _m_vfs_record_result(M_VFS_ERR_NOT_SUPPORTED);
    }

    m_vfs_read_cache_flush_file(file);

    portENTER_CRITICAL(&file->lock);
    file->closed = true;
    portEXIT_CRITICAL(&file->lock);

    m_vfs_file_notify_event(file);

    m_vfs_error_t err = M_VFS_ERR_OK;
    if (file->node != NULL && file->node->fs_type != NULL &&
            file->node->fs_type->ops != NULL &&
            file->node->fs_type->ops->close != NULL) {
        err = file->node->fs_type->ops->close(file);
    }

    m_vfs_fd_release(job, fd);
    return _m_vfs_record_result(err);
}

m_vfs_error_t
m_vfs_unlink(m_job_id_t job,
             const char *path)
{
    if (path == NULL) {
        return _m_vfs_record_result(M_VFS_ERR_INVALID_PARAM);
    }

    if (_m_vfs_should_inject(NULL)) {
        return _m_vfs_record_result(M_VFS_ERR_BUSY);
    }

    m_vfs_path_t parsed;
    m_vfs_error_t err = _m_vfs_parse_user_path(job, path, &parsed);
    if (err != M_VFS_ERR_OK) {
        return _m_vfs_record_result(err);
    }

    m_vfs_node_t *parent = NULL;
    char leaf[M_VFS_NAME_MAX_LEN];
    err = _m_vfs_resolve_parent(job,
                                             &parsed,
                                             &parent,
                                             leaf,
                                             sizeof(leaf));
    if (err != M_VFS_ERR_OK) {
        return _m_vfs_record_result(err);
    }

    if (parent->fs_type == NULL || parent->fs_type->ops == NULL ||
            parent->fs_type->ops->unlink == NULL) {
        m_vfs_node_release(parent);
        return _m_vfs_record_result(M_VFS_ERR_NOT_SUPPORTED);
    }

    err = parent->fs_type->ops->unlink(parent->mount, parent, leaf);
    m_vfs_node_release(parent);
    return _m_vfs_record_result(err);
}

m_vfs_error_t
m_vfs_mkdir(m_job_id_t job,
            const char *path,
            uint32_t mode)
{
    if (path == NULL) {
        return _m_vfs_record_result(M_VFS_ERR_INVALID_PARAM);
    }

    if (_m_vfs_should_inject(NULL)) {
        return _m_vfs_record_result(M_VFS_ERR_BUSY);
    }

    m_vfs_path_t parsed;
    m_vfs_error_t err = _m_vfs_parse_user_path(job, path, &parsed);
    if (err != M_VFS_ERR_OK) {
        return _m_vfs_record_result(err);
    }

    m_vfs_node_t *parent = NULL;
    char leaf[M_VFS_NAME_MAX_LEN];
    err = _m_vfs_resolve_parent(job,
                                             &parsed,
                                             &parent,
                                             leaf,
                                             sizeof(leaf));
    if (err != M_VFS_ERR_OK) {
        return _m_vfs_record_result(err);
    }

    if (parent->fs_type == NULL || parent->fs_type->ops == NULL ||
            parent->fs_type->ops->mkdir == NULL) {
        m_vfs_node_release(parent);
        return _m_vfs_record_result(M_VFS_ERR_NOT_SUPPORTED);
    }

    m_vfs_node_t *created = NULL;
    err = parent->fs_type->ops->mkdir(parent->mount,
                                      parent,
                                      leaf,
                                      mode,
                                      &created);
    m_vfs_node_release(parent);
    if (created != NULL) {
        m_vfs_node_release(created);
    }
    return _m_vfs_record_result(err);
}

m_vfs_error_t
m_vfs_chdir(m_job_id_t job,
            const char *path)
{
    if (job == NULL || path == NULL) {
        return _m_vfs_record_result(M_VFS_ERR_INVALID_PARAM);
    }

    m_vfs_path_t parsed;
    m_vfs_error_t err = _m_vfs_parse_user_path(job, path, &parsed);
    if (err != M_VFS_ERR_OK) {
        return _m_vfs_record_result(err);
    }

    m_vfs_node_t *node = NULL;
    err = m_vfs_path_resolve(job, &parsed, &node);
    if (err != M_VFS_ERR_OK) {
        return _m_vfs_record_result(err);
    }

    if (node == NULL || node->type != M_VFS_NODE_TYPE_DIRECTORY) {
        if (node != NULL) {
            m_vfs_node_release(node);
        }
        return _m_vfs_record_result(M_VFS_ERR_INVALID_PARAM);
    }

    char cwd_buf[JOB_CTX_CWD_MAX_LEN] = {0};
    strncpy(cwd_buf, parsed.normalized, sizeof(cwd_buf));
    cwd_buf[sizeof(cwd_buf) - 1] = '\0';

    job_ctx_error_t job_err = m_job_field_set(job,
                                              JOB_CTX_FIELD_CWD,
                                              cwd_buf,
                                              sizeof(cwd_buf));
    m_vfs_node_release(node);
    if (job_err != JOB_CTX_OK) {
        return _m_vfs_record_result(_m_vfs_job_error_to_vfs(job_err));
    }

    m_vfs_job_cwd_update(job, cwd_buf);
    return _m_vfs_record_result(M_VFS_ERR_OK);
}

m_vfs_error_t
m_vfs_getcwd(m_job_id_t job,
             char *buffer,
             size_t size)
{
    if (job == NULL || buffer == NULL || size == 0) {
        return _m_vfs_record_result(M_VFS_ERR_INVALID_PARAM);
    }

    char cwd[JOB_CTX_CWD_MAX_LEN] = {0};
    job_ctx_error_t job_err = m_job_field_get(job,
                                             JOB_CTX_FIELD_CWD,
                                             cwd,
                                             sizeof(cwd));
    if (job_err != JOB_CTX_OK) {
        return _m_vfs_record_result(_m_vfs_job_error_to_vfs(job_err));
    }

    size_t len = strnlen(cwd, sizeof(cwd));
    if (size <= len) {
        return _m_vfs_record_result(M_VFS_ERR_INVALID_PARAM);
    }

    memcpy(buffer, cwd, len);
    buffer[len] = '\0';
    return _m_vfs_record_result(M_VFS_ERR_OK);
}
