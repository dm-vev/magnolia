#include "kernel/arch/m_hw_init.h"

#include "sdkconfig.h"

#include "kernel/core/ipc/ipc.h"
#include "kernel/core/ipc/tests/ipc_signal_tests.h"
#include "kernel/core/job/tests/m_job_tests.h"
#include "kernel/core/sched/tests/m_sched_tests.h"
#include "kernel/core/sched/m_sched.h"
#include "kernel/core/timer/m_timer.h"
#include "kernel/core/timer/tests/m_timer_tests.h"

void magnolia_hw_init(void)
{
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
}
