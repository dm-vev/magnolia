#ifndef MAGNOLIA_VFS_M_VFS_WAIT_H
#define MAGNOLIA_VFS_M_VFS_WAIT_H

#include "kernel/core/ipc/ipc_scheduler_bridge.h"
#include "kernel/core/sched/m_sched.h"
#include "kernel/core/timer/m_timer.h"
#include "kernel/core/vfs/m_vfs_types.h"

#ifdef __cplusplus
extern "C" {
#endif

ipc_wait_result_t m_vfs_file_wait(m_vfs_file_t *file,
                                  m_sched_wait_reason_t reason,
                                  const m_timer_deadline_t *deadline);

void m_vfs_file_wake(m_vfs_file_t *file, ipc_wait_result_t result);
void m_vfs_file_notify_event(m_vfs_file_t *file);

#ifdef __cplusplus
}
#endif

#endif /* MAGNOLIA_VFS_M_VFS_WAIT_H */
