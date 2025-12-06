#ifndef MAGNOLIA_VFS_RAMFS_H
#define MAGNOLIA_VFS_RAMFS_H

#include "kernel/core/vfs/m_vfs_types.h"

#ifdef __cplusplus
extern "C" {
#endif

const m_vfs_fs_type_t *m_ramfs_fs_type(void);
void ramfs_diag_tree_snapshot(void (*callback)(const m_vfs_node_t *, void *),
                              void *user_data);

#ifdef __cplusplus
}
#endif

#endif /* MAGNOLIA_VFS_RAMFS_H */
