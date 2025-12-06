#include <stdbool.h>
#include <stddef.h>
#include <stdatomic.h>
#include <string.h>

#include "kernel/core/vfs/cache/m_vfs_read_cache.h"
#include "kernel/core/vfs/m_vfs_diag.h"
#include "kernel/core/vfs/core/m_vfs_object.h"
#include "kernel/core/vfs/core/m_vfs_registry.h"
#include "kernel/core/vfs/core/m_vfs_test.h"
#include "kernel/core/vfs/core/m_vfs_errno.h"
#include "kernel/core/vfs/fd/m_vfs_fd.h"
#include "kernel/core/vfs/ramfs/ramfs.h"

typedef struct {
    m_vfs_diag_fs_type_info_t *buffer;
    size_t capacity;
    size_t count;
} m_vfs_diag_fs_type_ctx_t;

static bool
_m_vfs_diag_fs_type_cb(const m_vfs_fs_type_t *type, void *user_data)
{
    if (type == NULL || user_data == NULL) {
        return true;
    }

    m_vfs_diag_fs_type_ctx_t *ctx = user_data;
    if (ctx->count >= ctx->capacity) {
        return false;
    }

    strncpy(ctx->buffer[ctx->count].name,
            type->name,
            M_VFS_FS_NAME_MAX_LEN);
    ctx->buffer[ctx->count].name[M_VFS_FS_NAME_MAX_LEN - 1] = '\0';
    ++ctx->count;
    return ctx->count < ctx->capacity;
}

size_t
m_vfs_diag_fs_types(m_vfs_diag_fs_type_info_t *buffer,
                     size_t capacity)
{
    if (buffer == NULL || capacity == 0) {
        return 0;
    }

    m_vfs_diag_fs_type_ctx_t ctx = {
        .buffer = buffer,
        .capacity = capacity,
        .count = 0,
    };

    m_vfs_registry_iterate_fs_types(_m_vfs_diag_fs_type_cb, &ctx);
    return ctx.count;
}

typedef struct {
    m_vfs_diag_mount_info_t *buffer;
    size_t capacity;
    size_t count;
} m_vfs_diag_mount_ctx_t;

static bool
_m_vfs_diag_mount_cb(m_vfs_mount_t *mount, void *user_data)
{
    if (mount == NULL || user_data == NULL) {
        return true;
    }

    m_vfs_diag_mount_ctx_t *ctx = user_data;
    if (ctx->count >= ctx->capacity) {
        return false;
    }

    strncpy(ctx->buffer[ctx->count].target,
            mount->target,
            M_VFS_PATH_MAX_LEN);
    ctx->buffer[ctx->count].target[M_VFS_PATH_MAX_LEN - 1] = '\0';
    if (mount->fs_type != NULL && mount->fs_type->name != NULL) {
        strncpy(ctx->buffer[ctx->count].fs_type,
                mount->fs_type->name,
                M_VFS_FS_NAME_MAX_LEN);
        ctx->buffer[ctx->count].fs_type[M_VFS_FS_NAME_MAX_LEN - 1] = '\0';
    } else {
        ctx->buffer[ctx->count].fs_type[0] = '\0';
    }
    ctx->buffer[ctx->count].active = mount->active;
    ctx->buffer[ctx->count].sequence = mount->sequence;
    ctx->buffer[ctx->count].index = mount->registry_index;
    size_t root_refcount = 0;
    if (mount->root != NULL) {
        root_refcount = atomic_load_explicit(&mount->root->refcount,
                                             memory_order_relaxed);
    }
    ctx->buffer[ctx->count].root_refcount = root_refcount;
    ++ctx->count;
    return ctx->count < ctx->capacity;
}

size_t
m_vfs_diag_mounts(m_vfs_diag_mount_info_t *buffer,
                   size_t capacity)
{
    if (buffer == NULL || capacity == 0) {
        return 0;
    }

    m_vfs_diag_mount_ctx_t ctx = {
        .buffer = buffer,
        .capacity = capacity,
        .count = 0,
    };

    m_vfs_registry_iterate_mounts(_m_vfs_diag_mount_cb, &ctx);
    return ctx.count;
}

typedef struct {
    void (*cb)(const m_vfs_mount_t *, void *);
    void *user_data;
} m_vfs_diag_mount_tree_ctx_t;

static bool
_m_vfs_diag_mount_tree_cb(m_vfs_mount_t *mount, void *user_data)
{
    if (mount == NULL || user_data == NULL) {
        return true;
    }

    m_vfs_diag_mount_tree_ctx_t *ctx = user_data;
    ctx->cb(mount, ctx->user_data);
    return true;
}

void
m_vfs_diag_mount_tree(void (*callback)(const m_vfs_mount_t *, void *),
                      void *user_data)
{
    if (callback == NULL) {
        return;
    }

    m_vfs_diag_mount_tree_ctx_t ctx = {
        .cb = callback,
        .user_data = user_data,
    };
    m_vfs_registry_iterate_mounts(_m_vfs_diag_mount_tree_cb, &ctx);
}

void
m_vfs_diag_job_cwds(m_vfs_job_cwd_iter_fn cb, void *user_data)
{
    if (cb == NULL) {
        return;
    }

    m_vfs_job_cwd_iterate(cb, user_data);
}

size_t
m_vfs_diag_job_fd_tables(void)
{
    return m_vfs_fd_job_table_count();
}

size_t
m_vfs_diag_kernel_fd_capacity(void)
{
    return m_vfs_fd_kernel_capacity();
}

void
m_vfs_diag_ramfs_tree(void (*callback)(const m_vfs_node_t *, void *),
                      void *user_data)
{
    ramfs_diag_tree_snapshot(callback, user_data);
}

size_t
m_vfs_diag_job_fd_snapshot(m_vfs_fd_job_table_snapshot_t *buffer,
                           size_t capacity)
{
    return m_vfs_fd_job_table_snapshot(buffer, capacity);
}

void
m_vfs_diag_open_files(m_vfs_fd_diag_iter_fn cb, void *user_data)
{
    if (cb == NULL) {
        return;
    }

    m_vfs_fd_foreach(cb, user_data);
}

void
m_vfs_diag_nodes(m_vfs_diag_node_iter_fn cb, void *user_data)
{
    if (cb == NULL) {
        return;
    }

    m_vfs_node_iterate(cb, user_data);
}

void
m_vfs_diag_read_cache_stats(m_vfs_read_cache_stats_t *stats)
{
    if (stats == NULL) {
        return;
    }
    m_vfs_read_cache_stats(stats);
}

void
m_vfs_diag_read_cache_flush(void)
{
    m_vfs_read_cache_flush_all();
}

void
m_vfs_diag_errno_snapshot(size_t *buffer, size_t capacity)
{
    m_vfs_errno_snapshot(buffer, capacity);
}

void
m_vfs_diag_error_injection(bool *enabled, m_vfs_error_t *code)
{
    if (enabled != NULL) {
        *enabled = m_vfs_test_error_injection_enabled();
    }
    if (code != NULL) {
        *code = m_vfs_test_error_injection_code();
    }
}
