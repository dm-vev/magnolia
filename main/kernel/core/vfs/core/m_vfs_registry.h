#ifndef MAGNOLIA_VFS_M_VFS_REGISTRY_H
#define MAGNOLIA_VFS_M_VFS_REGISTRY_H

#include <stdbool.h>
#include <stddef.h>

#include "kernel/core/vfs/m_vfs_types.h"

#ifdef __cplusplus
extern "C" {
#endif

void m_vfs_registry_init(void);

m_vfs_error_t m_vfs_registry_fs_type_register(const m_vfs_fs_type_t *type);
m_vfs_error_t m_vfs_registry_fs_type_unregister(const char *name);
const m_vfs_fs_type_t *m_vfs_registry_fs_type_find(const char *name);

m_vfs_error_t m_vfs_registry_mount_add(m_vfs_mount_t *mount);
m_vfs_mount_t *m_vfs_registry_mount_find(const char *target);
m_vfs_mount_t *m_vfs_registry_mount_best(const m_vfs_path_t *path,
                                          size_t *mutation_offset);
void m_vfs_registry_mount_remove(m_vfs_mount_t *mount);

size_t m_vfs_registry_fs_type_count(void);
size_t m_vfs_registry_mount_count(void);

void m_vfs_registry_iterate_fs_types(bool (*cb)(const m_vfs_fs_type_t *,
                                                void *),
                                      void *user_data);
void m_vfs_registry_iterate_mounts(bool (*cb)(m_vfs_mount_t *, void *),
                                   void *user_data);

#ifdef __cplusplus
}
#endif

#endif /* MAGNOLIA_VFS_M_VFS_REGISTRY_H */
