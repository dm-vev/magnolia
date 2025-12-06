#ifndef MAGNOLIA_VFS_M_VFS_READ_CACHE_H
#define MAGNOLIA_VFS_M_VFS_READ_CACHE_H

#include <stddef.h>

#include "kernel/core/vfs/m_vfs_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    size_t hits;
    size_t misses;
    size_t fills;
    size_t evictions;
    size_t entries;
    size_t block_size;
} m_vfs_read_cache_stats_t;

bool m_vfs_read_cache_enabled(void);
bool m_vfs_read_cache_enabled_for(const m_vfs_file_t *file);

m_vfs_error_t
m_vfs_read_cache_read(m_vfs_file_t *file,
                      void *buffer,
                      size_t size,
                      size_t *read,
                      m_vfs_error_t (*driver_read)(m_vfs_file_t *,
                                                   void *,
                                                   size_t,
                                                   size_t *));

void m_vfs_read_cache_flush_file(const m_vfs_file_t *file);
void m_vfs_read_cache_flush_all(void);
void m_vfs_read_cache_stats(m_vfs_read_cache_stats_t *stats);

#ifdef __cplusplus
}
#endif

#endif /* MAGNOLIA_VFS_M_VFS_READ_CACHE_H */
