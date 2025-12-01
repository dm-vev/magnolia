/*
 * Magnolia OS — IPC Subsystem
 * Purpose:
 *     Magnolia entry point.
 * Description:
 *     Initialize Magnolia core subsystems and launch the architecture-specific
 *     kernel task.
 *
 * © 2025 Magnolia Project
 */

#include "esp_log.h"
#include "kernel/arch/m_arch.h"
#include "kernel/core/ipc/ipc.h"
#include "kernel/core/ipc/tests/ipc_signal_tests.h"
#include "kernel/core/job/tests/m_job_tests.h"
#include "kernel/core/sched/tests/m_sched_tests.h"
#include "kernel/core/sched/m_sched.h"
#include "kernel/core/timer/m_timer.h"

static const char *TAG = "magnolia";

void app_main(void)
{
    ESP_LOGI(TAG, "Hello, Magnolia!");

    m_timer_init();
    m_sched_init();
    ipc_init();

#ifdef CONFIG_MAGNOLIA_SCHED_SELFTESTS
    m_sched_selftests_run();
#endif

#ifdef CONFIG_MAGNOLIA_IPC_SELFTESTS
    ipc_selftests_run();
#endif

#ifdef CONFIG_MAGNOLIA_JOB_SELFTESTS
    m_job_selftests_run();
#endif

    m_arch_start();
}
