#include "kernel/arch/m_hw_init.h"

#include "sdkconfig.h"

#include "kernel/core/ipc/ipc.h"
#include "kernel/core/ipc/tests/ipc_signal_tests.h"
#include "kernel/core/job/tests/m_job_tests.h"
#include "kernel/core/sched/tests/m_sched_tests.h"
#include "kernel/core/sched/m_sched.h"
#include "kernel/core/timer/m_timer.h"
#include "kernel/core/timer/tests/m_timer_tests.h"
#include "kernel/core/memory/m_alloc.h"
#if CONFIG_MAGNOLIA_VFS_DEVFS && CONFIG_MAGNOLIA_DEVFS_SELFTESTS
#include "kernel/vfs/fs/devfs/devfs_tests.h"
#endif
#if CONFIG_MAGNOLIA_LITTLEFS_ENABLED && CONFIG_MAGNOLIA_VFS_LITTLEFS_SELFTESTS
#include "kernel/vfs/fs/littlefs/littlefs_tests.h"
#endif
#if CONFIG_MAGNOLIA_ALLOC_ENABLED && CONFIG_MAGNOLIA_ALLOC_SELFTESTS
#include "kernel/core/memory/tests/m_alloc_tests.h"
#endif
#if CONFIG_MAGNOLIA_ELF_ENABLED && CONFIG_MAGNOLIA_ELF_SELFTESTS
#include "kernel/core/elf/tests/m_elf_tests.h"
#endif

void magnolia_hw_init(void)
{
    m_alloc_init();
    m_timer_init();
    m_sched_init();
    ipc_init();

#ifdef CONFIG_MAGNOLIA_SCHED_SELFTESTS
    m_sched_selftests_run();
#endif

#ifdef CONFIG_MAGNOLIA_TIMER_SELFTESTS
    m_timer_selftests_run();
#endif

#if CONFIG_MAGNOLIA_IPC_ENABLED && CONFIG_MAGNOLIA_IPC_SELFTESTS
    ipc_selftests_run();
#endif

#if CONFIG_MAGNOLIA_JOB_ENABLED && CONFIG_MAGNOLIA_JOB_SELFTESTS
    m_job_selftests_run();
#endif

#if CONFIG_MAGNOLIA_ALLOC_ENABLED && CONFIG_MAGNOLIA_ALLOC_SELFTESTS
    m_alloc_selftests_run();
#endif

#if CONFIG_MAGNOLIA_VFS_DEVFS && CONFIG_MAGNOLIA_DEVFS_SELFTESTS
    devfs_selftests_run();
#endif

#if CONFIG_MAGNOLIA_LITTLEFS_ENABLED && CONFIG_MAGNOLIA_VFS_LITTLEFS_SELFTESTS
    littlefs_selftests_run();
#endif

#if CONFIG_MAGNOLIA_ELF_ENABLED && CONFIG_MAGNOLIA_ELF_SELFTESTS
    m_elf_selftests_run();
#endif
}
