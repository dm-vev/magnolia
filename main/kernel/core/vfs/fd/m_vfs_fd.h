#ifndef MAGNOLIA_VFS_M_VFS_FD_H
#define MAGNOLIA_VFS_M_VFS_FD_H

#include <stddef.h>
#include <stdint.h>

#include "kernel/core/job/jctx_public.h"
#include "kernel/core/vfs/m_vfs_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    m_job_id_t job;
    int fd;
    const m_vfs_file_t *file;
} m_vfs_fd_diag_entry_t;

typedef struct {
    m_job_id_t job;
    size_t used;
} m_vfs_fd_job_table_snapshot_t;

void m_vfs_fd_init(void);

int m_vfs_fd_allocate(m_job_id_t job, m_vfs_file_t *file);
m_vfs_file_t *m_vfs_fd_lookup(m_job_id_t job, int fd);
void m_vfs_fd_release(m_job_id_t job, int fd);
m_vfs_error_t m_vfs_fd_assign(m_job_id_t job, int fd, m_vfs_file_t *file);

size_t m_vfs_fd_kernel_capacity(void);
size_t m_vfs_fd_job_table_count(void);

typedef bool (*m_vfs_fd_diag_iter_fn)(m_job_id_t job,
                                      int fd,
                                      const m_vfs_file_t *file,
                                      void *user_data);

void m_vfs_fd_foreach(m_vfs_fd_diag_iter_fn cb, void *user_data);
size_t m_vfs_fd_job_table_snapshot(m_vfs_fd_job_table_snapshot_t *buffer,
                                   size_t capacity);

void m_vfs_fd_close_mount_fds(m_vfs_mount_t *mount);

#ifdef __cplusplus
}
#endif

#endif /* MAGNOLIA_VFS_M_VFS_FD_H */
