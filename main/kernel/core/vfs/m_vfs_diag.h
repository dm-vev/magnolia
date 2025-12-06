/**
 * @file        kernel/core/vfs/m_vfs_diag.h
 * @brief       Diagnostics helpers for the Magnolia VFS registry.
 */
#ifndef MAGNOLIA_VFS_M_VFS_DIAG_H
#define MAGNOLIA_VFS_M_VFS_DIAG_H

#include <stdbool.h>
#include <stddef.h>

#include "kernel/core/vfs/cache/m_vfs_read_cache.h"
#include "kernel/core/vfs/m_vfs_types.h"
#include "kernel/core/vfs/fd/m_vfs_fd.h"
#include "kernel/core/vfs/core/m_vfs_jobcwd.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    char name[M_VFS_FS_NAME_MAX_LEN];
} m_vfs_diag_fs_type_info_t;

typedef struct {
    char target[M_VFS_PATH_MAX_LEN];
    char fs_type[M_VFS_FS_NAME_MAX_LEN];
    bool active;
    uint32_t sequence;
    size_t index;
    size_t root_refcount;
} m_vfs_diag_mount_info_t;

size_t m_vfs_diag_fs_types(m_vfs_diag_fs_type_info_t *buffer,
                            size_t capacity);
size_t m_vfs_diag_mounts(m_vfs_diag_mount_info_t *buffer,
                          size_t capacity);
size_t m_vfs_diag_job_fd_tables(void);
size_t m_vfs_diag_kernel_fd_capacity(void);
void m_vfs_diag_ramfs_tree(void (*callback)(const m_vfs_node_t *, void *),
                           void *user_data);

size_t m_vfs_diag_job_fd_snapshot(m_vfs_fd_job_table_snapshot_t *buffer,
                                  size_t capacity);
void m_vfs_diag_open_files(m_vfs_fd_diag_iter_fn cb, void *user_data);

typedef bool (*m_vfs_diag_node_iter_fn)(const m_vfs_node_t *node,
                                        void *user_data);
void m_vfs_diag_nodes(m_vfs_diag_node_iter_fn cb, void *user_data);

void m_vfs_diag_read_cache_stats(m_vfs_read_cache_stats_t *stats);
void m_vfs_diag_read_cache_flush(void);

void m_vfs_diag_errno_snapshot(size_t *buffer, size_t capacity);

/**
 * @brief Report on the current error-injection state used for VFS tests.
 */
void m_vfs_diag_error_injection(bool *enabled, m_vfs_error_t *code);

void m_vfs_diag_mount_tree(void (*callback)(const m_vfs_mount_t *, void *),
                           void *user_data);

void m_vfs_diag_job_cwds(m_vfs_job_cwd_iter_fn cb, void *user_data);

#ifdef __cplusplus
}
#endif

#endif /* MAGNOLIA_VFS_M_VFS_DIAG_H */
