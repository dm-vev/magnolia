#ifndef MAGNOLIA_VFS_M_VFS_OBJECT_H
#define MAGNOLIA_VFS_M_VFS_OBJECT_H

#include <stdbool.h>
#include <stddef.h>

#include "kernel/core/vfs/m_vfs_types.h"

#ifdef __cplusplus
extern "C" {
#endif

m_vfs_node_t *m_vfs_node_create(m_vfs_mount_t *mount,
                                 m_vfs_node_type_t type);
void m_vfs_node_acquire(m_vfs_node_t *node);
void m_vfs_node_release(m_vfs_node_t *node);

typedef bool (*m_vfs_node_iter_fn)(const m_vfs_node_t *node,
                                   void *user_data);
void m_vfs_node_iterate(m_vfs_node_iter_fn cb, void *user_data);

m_vfs_file_t *m_vfs_file_create(m_vfs_node_t *node);
void m_vfs_file_acquire(m_vfs_file_t *file);
void m_vfs_file_release(m_vfs_file_t *file);

void m_vfs_file_set_offset(m_vfs_file_t *file, size_t offset);

#if CONFIG_MAGNOLIA_VFS_NODE_LIFETIME_CHECK
void m_vfs_node_lifetime_check_report(void);
#endif

#ifdef __cplusplus
}
#endif

#endif /* MAGNOLIA_VFS_M_VFS_OBJECT_H */
