/**
 * @file        kernel/core/vfs/m_vfs.h
 * @brief       Public Magnolia VFS API.
 * @details     Exposes the VFS lifecycle, path helpers, and job-aware APIs used by
 *              consumers and filesystem drivers.
 */
#ifndef MAGNOLIA_VFS_M_VFS_H
#define MAGNOLIA_VFS_M_VFS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "sdkconfig.h"
#include "kernel/core/job/jctx_public.h"
#include "kernel/core/timer/m_timer.h"
#include "kernel/core/vfs/m_vfs_types.h"

#define M_VFS_POLLIN  (1u << 0)
#define M_VFS_POLLOUT (1u << 1)
#define M_VFS_POLLERR (1u << 2)
#define M_VFS_POLLHUP (1u << 3)

typedef struct {
    int fd;
    uint32_t events;
    uint32_t revents;
} m_vfs_pollfd_t;

#ifdef __cplusplus
extern "C" {
#endif

m_vfs_error_t m_vfs_init(void);

/**
 * @brief Register or unregister filesystem drivers with the VFS core.
 */
m_vfs_error_t m_vfs_fs_type_register(const m_vfs_fs_type_t *type);
m_vfs_error_t m_vfs_fs_type_unregister(const char *name);
const m_vfs_fs_type_t *m_vfs_fs_type_find(const char *name);

m_vfs_error_t m_vfs_mount(const char *target,
                          const char *fs_type,
                          void *options);
m_vfs_error_t m_vfs_unmount(const char *target);
m_vfs_error_t m_vfs_unmount_force(const char *target);

m_vfs_error_t m_vfs_open(m_job_id_t job,
                         const char *path,
                         int flags,
                         int *out_fd);
m_vfs_error_t m_vfs_read(m_job_id_t job,
                         int fd,
                         void *buffer,
                         size_t size,
                         size_t *read);
m_vfs_error_t m_vfs_read_timed(m_job_id_t job,
                               int fd,
                               void *buffer,
                               size_t size,
                               size_t *read,
                               const m_timer_deadline_t *deadline);
m_vfs_error_t m_vfs_write(m_job_id_t job,
                          int fd,
                          const void *buffer,
                          size_t size,
                          size_t *written);
m_vfs_error_t m_vfs_write_timed(m_job_id_t job,
                                int fd,
                                const void *buffer,
                                size_t size,
                                size_t *written,
                                const m_timer_deadline_t *deadline);
m_vfs_error_t m_vfs_dup(m_job_id_t job,
                        int oldfd,
                        int *out_fd);
m_vfs_error_t m_vfs_dup2(m_job_id_t job,
                         int oldfd,
                         int newfd);
m_vfs_error_t m_vfs_poll(m_job_id_t job,
                         m_vfs_pollfd_t *fds,
                         size_t count,
                         const m_timer_deadline_t *deadline,
                         size_t *ready);
m_vfs_error_t m_vfs_readdir(m_job_id_t job,
                            int fd,
                            m_vfs_dirent_t *entries,
                            size_t capacity,
                            size_t *populated);
m_vfs_error_t m_vfs_ioctl(m_job_id_t job,
                          int fd,
                          unsigned long request,
                          void *arg);
m_vfs_error_t m_vfs_close(m_job_id_t job,
                          int fd);
m_vfs_error_t m_vfs_unlink(m_job_id_t job,
                            const char *path);
m_vfs_error_t m_vfs_mkdir(m_job_id_t job,
                           const char *path,
                           uint32_t mode);

m_vfs_error_t m_vfs_chdir(m_job_id_t job, const char *path);
m_vfs_error_t m_vfs_getcwd(m_job_id_t job, char *buffer, size_t size);

bool m_vfs_path_normalize(const char *path,
                          char *out,
                          size_t capacity);
bool m_vfs_path_parse(const char *path,
                      m_vfs_path_t *result);
m_vfs_error_t m_vfs_path_resolve(m_job_id_t job,
                                 const m_vfs_path_t *path,
                                 m_vfs_node_t **out_node);

#ifdef __cplusplus
}
#endif

#endif /* MAGNOLIA_VFS_M_VFS_H */
