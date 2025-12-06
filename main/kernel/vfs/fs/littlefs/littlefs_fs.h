#ifndef MAGNOLIA_VFS_LITTLEFS_FS_H
#define MAGNOLIA_VFS_LITTLEFS_FS_H

#include "kernel/core/vfs/m_vfs_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    bool format_if_empty;
    bool force_format;
} littlefs_mount_options_t;

const m_vfs_fs_type_t *m_littlefs_fs_type(void);

#ifdef __cplusplus
}
#endif

#endif /* MAGNOLIA_VFS_LITTLEFS_FS_H */
