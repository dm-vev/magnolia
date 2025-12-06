#ifndef MAGNOLIA_VFS_LITTLEFS_BACKEND_H
#define MAGNOLIA_VFS_LITTLEFS_BACKEND_H

#include "lfs.h"

#ifdef __cplusplus
extern "C" {
#endif

int littlefs_backend_read(const struct lfs_config *c,
                          lfs_block_t block,
                          lfs_off_t off,
                          void *buffer,
                          lfs_size_t size);
int littlefs_backend_prog(const struct lfs_config *c,
                          lfs_block_t block,
                          lfs_off_t off,
                          const void *buffer,
                          lfs_size_t size);
int littlefs_backend_erase(const struct lfs_config *c, lfs_block_t block);
int littlefs_backend_sync(const struct lfs_config *c);

#ifdef __cplusplus
}
#endif

#endif /* MAGNOLIA_VFS_LITTLEFS_BACKEND_H */
