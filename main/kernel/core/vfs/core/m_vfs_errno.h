#ifndef MAGNOLIA_VFS_M_VFS_ERRNO_H
#define MAGNOLIA_VFS_M_VFS_ERRNO_H

#include <stddef.h>

typedef enum m_vfs_error m_vfs_error_t;

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    M_EOK = 0,
    M_ENOENT,
    M_EIO,
    M_EPERM,
    M_EBADF,
    M_EINVAL,
    M_EEXIST,
    M_ENOTDIR,
    M_EISDIR,
    M_ENOSPC,
    M_ENOTSUP,
    M_EUNKNOWN,
    M_VFS_ERRNO_COUNT,
} m_vfs_errno_t;

const char *m_vfs_errno_name(m_vfs_errno_t err);
void m_vfs_errno_record(m_vfs_errno_t err);

m_vfs_errno_t m_vfs_errno_from_vfs_error(m_vfs_error_t err);
m_vfs_error_t m_vfs_error_from_errno(m_vfs_errno_t err);
m_vfs_error_t m_vfs_from_errno(m_vfs_errno_t err);
m_vfs_error_t m_vfs_record_error(m_vfs_error_t err);

void m_vfs_errno_snapshot(size_t *buffer, size_t capacity);
void m_vfs_errno_reset(void);

#ifdef __cplusplus
}
#endif

#endif /* MAGNOLIA_VFS_M_VFS_ERRNO_H */
