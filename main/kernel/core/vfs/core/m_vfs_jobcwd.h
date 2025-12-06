#ifndef MAGNOLIA_VFS_M_VFS_JOBCD_H
#define MAGNOLIA_VFS_M_VFS_JOBCD_H

#include <stdbool.h>

#include "kernel/core/job/jctx_public.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef bool (*m_vfs_job_cwd_iter_fn)(m_job_id_t job,
                                      const char *cwd,
                                      void *user_data);

void m_vfs_job_cwd_init(void);
void m_vfs_job_cwd_update(m_job_id_t job, const char *cwd);
void m_vfs_job_cwd_remove(m_job_id_t job);
void m_vfs_job_cwd_iterate(m_vfs_job_cwd_iter_fn cb, void *user_data);

#ifdef __cplusplus
}
#endif

#endif /* MAGNOLIA_VFS_M_VFS_JOBCD_H */
