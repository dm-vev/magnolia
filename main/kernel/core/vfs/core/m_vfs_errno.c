#include <stddef.h>
#include <stdatomic.h>

#include "kernel/core/vfs/core/m_vfs_errno.h"
#include "kernel/core/vfs/m_vfs_types.h"

static atomic_size_t g_vfs_errno_counters[M_VFS_ERRNO_COUNT];

const char *
m_vfs_errno_name(m_vfs_errno_t err)
{
    static const char *const names[M_VFS_ERRNO_COUNT] = {
        [M_EOK] = "M_EOK",
        [M_ENOENT] = "M_ENOENT",
        [M_EIO] = "M_EIO",
        [M_EPERM] = "M_EPERM",
        [M_EBADF] = "M_EBADF",
        [M_EINVAL] = "M_EINVAL",
        [M_EEXIST] = "M_EEXIST",
        [M_ENOTDIR] = "M_ENOTDIR",
        [M_EISDIR] = "M_EISDIR",
        [M_ENOSPC] = "M_ENOSPC",
        [M_ENOTSUP] = "M_ENOTSUP",
        [M_EUNKNOWN] = "M_EUNKNOWN",
    };

    if ((size_t)err >= M_VFS_ERRNO_COUNT) {
        return "M_EUNKNOWN";
    }
    const char *name = names[err];
    return (name != NULL) ? name : "M_EUNKNOWN";
}

void
m_vfs_errno_record(m_vfs_errno_t err)
{
    if ((size_t)err >= M_VFS_ERRNO_COUNT) {
        err = M_EUNKNOWN;
    }
    atomic_fetch_add_explicit(&g_vfs_errno_counters[err],
                              1,
                              memory_order_relaxed);
}

m_vfs_errno_t
m_vfs_errno_from_vfs_error(m_vfs_error_t err)
{
    switch (err) {
    case M_VFS_ERR_OK:
        return M_EOK;
    case M_VFS_ERR_INVALID_PARAM:
    case M_VFS_ERR_INVALID_PATH:
        return M_EINVAL;
    case M_VFS_ERR_NOT_FOUND:
        return M_ENOENT;
    case M_VFS_ERR_NOT_SUPPORTED:
        return M_ENOTSUP;
    case M_VFS_ERR_NO_MEMORY:
    case M_VFS_ERR_TOO_MANY_ENTRIES:
        return M_ENOSPC;
    case M_VFS_ERR_BUSY:
        return M_EPERM;
    case M_VFS_ERR_INTERRUPTED:
    case M_VFS_ERR_WOULD_BLOCK:
    case M_VFS_ERR_TIMEOUT:
    case M_VFS_ERR_IO:
        return M_EIO;
    case M_VFS_ERR_DESTROYED:
        return M_EBADF;
    default:
        return M_EUNKNOWN;
    }
}

m_vfs_error_t
m_vfs_error_from_errno(m_vfs_errno_t err)
{
    switch (err) {
    case M_EOK:
        return M_VFS_ERR_OK;
    case M_ENOENT:
        return M_VFS_ERR_NOT_FOUND;
    case M_EIO:
        return M_VFS_ERR_IO;
    case M_EPERM:
    case M_EEXIST:
        return M_VFS_ERR_BUSY;
    case M_EBADF:
        return M_VFS_ERR_INVALID_PARAM;
    case M_EINVAL:
        return M_VFS_ERR_INVALID_PARAM;
    case M_ENOTDIR:
    case M_EISDIR:
        return M_VFS_ERR_INVALID_PATH;
    case M_ENOSPC:
        return M_VFS_ERR_NO_MEMORY;
    case M_ENOTSUP:
        return M_VFS_ERR_NOT_SUPPORTED;
    default:
        return M_VFS_ERR_INTERRUPTED;
    }
}

m_vfs_error_t
m_vfs_from_errno(m_vfs_errno_t err)
{
    m_vfs_errno_record(err);
    return m_vfs_error_from_errno(err);
}

m_vfs_error_t
m_vfs_record_error(m_vfs_error_t err)
{
    m_vfs_errno_record(m_vfs_errno_from_vfs_error(err));
    return err;
}

void
m_vfs_errno_snapshot(size_t *buffer, size_t capacity)
{
    if (buffer == NULL || capacity == 0) {
        return;
    }

    size_t limit = (capacity < M_VFS_ERRNO_COUNT) ?
                   capacity : M_VFS_ERRNO_COUNT;
    for (size_t i = 0; i < limit; ++i) {
        buffer[i] = atomic_load_explicit(&g_vfs_errno_counters[i],
                                         memory_order_relaxed);
    }
}

void
m_vfs_errno_reset(void)
{
    for (size_t i = 0; i < M_VFS_ERRNO_COUNT; ++i) {
        atomic_store_explicit(&g_vfs_errno_counters[i], 0,
                              memory_order_relaxed);
    }
}
