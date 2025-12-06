/**
 * @file        kernel/core/vfs/path/m_vfs_path.h
 * @brief       Path helper prototypes used across the Magnolia VFS core.
 */
#ifndef MAGNOLIA_VFS_M_VFS_PATH_H
#define MAGNOLIA_VFS_M_VFS_PATH_H

#include <stdbool.h>
#include <stddef.h>

#include "kernel/core/job/jctx_public.h"
#include "kernel/core/vfs/m_vfs_types.h"

#ifdef __cplusplus
extern "C" {
#endif

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

#endif /* MAGNOLIA_VFS_M_VFS_PATH_H */
