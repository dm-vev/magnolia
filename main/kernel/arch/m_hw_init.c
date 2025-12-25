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
#include "kernel/core/vfs/m_vfs.h"
#include "kernel/vfs/fs/littlefs/littlefs_fs.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
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
#if CONFIG_MAGNOLIA_ELF_ENABLED && CONFIG_MAGNOLIA_ELF_AUTOSTART_INIT
#include "kernel/core/elf/m_elf_loader.h"
#include "kernel/core/job/m_job.h"
#endif

static const char *TAG = "m_hw_init";

#ifndef CONFIG_MAGNOLIA_ELF_INIT_STACK_DEPTH
/* Fallback for builds that haven't regenerated sdkconfig.h yet. */
#define CONFIG_MAGNOLIA_ELF_INIT_STACK_DEPTH 65536
#endif

#if CONFIG_MAGNOLIA_ELF_ENABLED && CONFIG_MAGNOLIA_ELF_AUTOSTART_INIT
static m_job_queue_t *s_init_queue;

static m_job_handler_result_t magnolia_init_job(m_job_id_t job, void *data)
{
    const char *path = (const char *)data;
    if (path == NULL) {
        return (m_job_handler_result_t){
            .status = M_JOB_RESULT_ERROR,
            .payload = NULL,
            .payload_size = 0,
        };
    }
    char *argv[] = { (char *)"init", NULL };

    while (1) {
        if (job != NULL && job->cancelled) {
            break;
        }
        int rc = 0;
        int ret = m_elf_run_file(path, 1, argv, &rc);
        ESP_LOGW(TAG, "init exited ret=%d rc=%d, restarting", ret, rc);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }

    return (m_job_handler_result_t){
        .status = M_JOB_RESULT_CANCELLED,
        .payload = NULL,
        .payload_size = 0,
    };
}

static void magnolia_autostart_init(void)
{
    if (s_init_queue != NULL) {
        return;
    }

    m_job_queue_config_t cfg = M_JOB_QUEUE_CONFIG_DEFAULT;
    cfg.name = "init";
    cfg.capacity = 1;
    cfg.worker_count = 1;
    cfg.stack_depth = CONFIG_MAGNOLIA_ELF_INIT_STACK_DEPTH;

    s_init_queue = m_job_queue_create(&cfg);
    if (s_init_queue == NULL) {
        ESP_LOGE(TAG, "init queue create failed");
        return;
    }

    const char *init_path = CONFIG_MAGNOLIA_ELF_INIT_PATH;
    m_job_error_t err = m_job_queue_submit(s_init_queue, magnolia_init_job, (void *)init_path);
    if (err != M_JOB_OK) {
        ESP_LOGE(TAG, "init submit failed err=%d", (int)err);
    }
}
#endif

#if CONFIG_MAGNOLIA_VFS_ENABLED && CONFIG_MAGNOLIA_LITTLEFS_ENABLED
static void magnolia_mount_rootfs(void)
{
    (void)m_vfs_init();

    littlefs_mount_options_t opts = {
        .partition_label = CONFIG_MAGNOLIA_LITTLEFS_PARTITION_LABEL,
        .format_if_mount_fails = CONFIG_MAGNOLIA_LITTLEFS_FORMAT_IF_FAIL,
        .read_only = false,
        .format_if_empty = CONFIG_MAGNOLIA_LITTLEFS_FORMAT_IF_FAIL,
        .force_format = false,
    };

#if CONFIG_MAGNOLIA_ELF_ENABLED && CONFIG_MAGNOLIA_ELF_APPLETS_SELFTESTS
    /*
     * When we expect a prebuilt applet filesystem, avoid formatting on failure,
     * so missing/invalid images are visible instead of being silently wiped.
     */
    opts.format_if_mount_fails = false;
    opts.format_if_empty = false;
    opts.force_format = false;
#endif

    (void)m_vfs_mount("/", "littlefs", &opts);
}
#endif

#if CONFIG_MAGNOLIA_LITTLEFS_ENABLED && CONFIG_MAGNOLIA_VFS_LITTLEFS_SELFTESTS
#ifndef CONFIG_MAGNOLIA_LITTLEFS_SELFTEST_TASK_STACK_DEPTH
#define CONFIG_MAGNOLIA_LITTLEFS_SELFTEST_TASK_STACK_DEPTH 4096
#endif

static void littlefs_selftests_task(void *arg)
{
    SemaphoreHandle_t done = (SemaphoreHandle_t)arg;
    littlefs_selftests_run();
    if (done) {
        xSemaphoreGive(done);
    }
    vTaskDelete(NULL);
}

static void run_littlefs_selftests(void)
{
    SemaphoreHandle_t done = xSemaphoreCreateBinary();
    if (done == NULL) {
        littlefs_selftests_run();
        return;
    }

    if (xTaskCreate(
            littlefs_selftests_task,
            "lfs_tests",
            CONFIG_MAGNOLIA_LITTLEFS_SELFTEST_TASK_STACK_DEPTH,
            done,
            5,
            NULL) != pdPASS) {
        vSemaphoreDelete(done);
        littlefs_selftests_run();
        return;
    }

    (void)xSemaphoreTake(done, portMAX_DELAY);
    vSemaphoreDelete(done);
}
#endif

void magnolia_hw_init(void)
{
    m_alloc_init();
    m_timer_init();
    m_sched_init();
    ipc_init();

#if CONFIG_MAGNOLIA_VFS_ENABLED && CONFIG_MAGNOLIA_LITTLEFS_ENABLED
    magnolia_mount_rootfs();
#endif

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
    run_littlefs_selftests();
#endif

#if CONFIG_MAGNOLIA_ELF_ENABLED && CONFIG_MAGNOLIA_ELF_SELFTESTS
    m_elf_selftests_run();
#endif

#if CONFIG_MAGNOLIA_ELF_ENABLED && CONFIG_MAGNOLIA_ELF_AUTOSTART_INIT
    magnolia_autostart_init();
#endif
}
