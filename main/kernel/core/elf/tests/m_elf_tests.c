/*
 * ELF loader selftests for Magnolia kernel.
 * Mirrors style of other kernel selftests.
 */

#include "sdkconfig.h"

#if CONFIG_MAGNOLIA_ELF_ENABLED && CONFIG_MAGNOLIA_ELF_SELFTESTS

#include <string.h>
#include <sys/errno.h>

#include "esp_log.h"

#include "kernel/core/elf/m_elf_loader.h"
#include "kernel/core/elf/tests/m_elf_tests.h"
#if CONFIG_MAGNOLIA_JOB_ENABLED
#include "kernel/core/job/m_job.h"
#endif

#ifndef CONFIG_MAGNOLIA_ELF_SELFTEST_APPLET_PATH
#define CONFIG_MAGNOLIA_ELF_SELFTEST_APPLET_PATH "/bin/elftest"
#endif

static const char *TAG = "elf_tests";

#define ELF_TEST_ASSERT(cond, label, fmt, ...) \
    do { \
        if (!(cond)) { \
            ESP_LOGE(TAG, "FAIL: " fmt, ##__VA_ARGS__); \
            goto label; \
        } \
    } while (0)

#if CONFIG_MAGNOLIA_JOB_ENABLED
typedef enum {
    ELF_RUN_KIND_BUFFER = 0,
    ELF_RUN_KIND_FILE = 1,
} elf_run_kind_t;

typedef struct {
    elf_run_kind_t kind;
    const char *path;
    const uint8_t *buffer;
    size_t size;
    int argc;
    char **argv;
    int ret;
    int rc;
} elf_run_request_t;

static m_job_handler_result_t elf_run_job(m_job_handle_t *job, void *data)
{
    (void)job;
    elf_run_request_t *req = (elf_run_request_t *)data;
    if (req == NULL) {
        return m_job_result_error(NULL, 0);
    }

    int app_rc = -1;
    int run_ret = -EINVAL;
    if (req->kind == ELF_RUN_KIND_FILE) {
        run_ret = m_elf_run_file(req->path, req->argc, req->argv, &app_rc);
    } else {
        run_ret = m_elf_run_buffer(req->buffer, req->size, req->argc, req->argv, &app_rc);
    }
    req->ret = run_ret;
    req->rc = app_rc;
    return (run_ret == 0) ? m_job_result_success(NULL, 0)
                          : m_job_result_error(NULL, 0);
}

static bool elf_run_request_via_job(elf_run_request_t *req)
{
    if (req == NULL) {
        return false;
    }

    m_job_queue_config_t cfg = M_JOB_QUEUE_CONFIG_DEFAULT;
    cfg.name = "elf_tests";
    cfg.capacity = 1;
    cfg.worker_count = 1;

    m_job_queue_t *queue = m_job_queue_create(&cfg);
    if (queue == NULL) {
        ESP_LOGE(TAG, "m_job_queue_create failed");
        return false;
    }

    m_job_handle_t *handle = NULL;
    if (m_job_queue_submit_with_handle(queue, elf_run_job, req, &handle) != M_JOB_OK
        || handle == NULL) {
        ESP_LOGE(TAG, "job submit failed");
        (void)m_job_queue_destroy(queue);
        return false;
    }

    m_job_result_descriptor_t result = {0};
    m_job_future_wait_result_t wait_res = m_job_wait_for_job(handle, &result);
    (void)result;
    (void)m_job_handle_destroy(handle);
    (void)m_job_queue_destroy(queue);

    if (wait_res != M_JOB_FUTURE_WAIT_OK) {
        ESP_LOGE(TAG, "job wait failed (%d)", (int)wait_res);
        return false;
    }

    return true;
}
#endif

#if CONFIG_IDF_TARGET_ARCH_XTENSA
extern const uint8_t test_elf_start[] asm("_binary_kernel_test_xtensa_elf_start") __attribute__((weak));
extern const uint8_t test_elf_end[]   asm("_binary_kernel_test_xtensa_elf_end") __attribute__((weak));
#elif CONFIG_IDF_TARGET_ARCH_RISCV
extern const uint8_t test_elf_start[] asm("_binary_kernel_test_riscv_elf_start") __attribute__((weak));
extern const uint8_t test_elf_end[]   asm("_binary_kernel_test_riscv_elf_end") __attribute__((weak));
#else
static const uint8_t *test_elf_start;
static const uint8_t *test_elf_end;
#endif

static bool test_invalid_magic(void)
{
    uint8_t buf[8] = {0};
    m_elf_t elf;
    m_elf_init(&elf, NULL);
    int ret = m_elf_relocate(&elf, buf, sizeof(buf));
    m_elf_deinit(&elf);
    return (ret == -EINVAL || ret == -ENOTSUP);
}

static bool test_run_embedded(void)
{
    const uint8_t *start = &test_elf_start[0];
    const uint8_t *end = &test_elf_end[0];
    if (!start || !end || end <= start) {
        ESP_LOGW(TAG, "No embedded test ELF found, skipping run test");
        return true;
    }

    size_t len = (size_t)(end - start);
    int rc = 0;
    int ret = 0;
#if CONFIG_MAGNOLIA_JOB_ENABLED
    elf_run_request_t req = {
        .kind = ELF_RUN_KIND_BUFFER,
        .path = NULL,
        .buffer = start,
        .size = len,
        .argc = 0,
        .argv = NULL,
        .ret = -1,
        .rc = -1,
    };
    if (!elf_run_request_via_job(&req)) {
        return false;
    }
    ret = req.ret;
    rc = req.rc;
#else
    ret = m_elf_run_buffer(start, len, 0, NULL, &rc);
#endif
    if (ret < 0) {
        ESP_LOGE(TAG, "m_elf_run_buffer failed errno=%d", ret);
        return false;
    }
    ESP_LOGI(TAG, "embedded ELF rc=%d", rc);
    return (rc >= 0);
}

#if CONFIG_MAGNOLIA_ELF_ENABLED && CONFIG_MAGNOLIA_ELF_APPLETS_SELFTESTS
static bool test_run_applet(const char *path)
{
    int rc = -1;
    int ret = 0;
#if CONFIG_MAGNOLIA_JOB_ENABLED
    elf_run_request_t req = {
        .kind = ELF_RUN_KIND_FILE,
        .path = path,
        .buffer = NULL,
        .size = 0,
        .argc = 0,
        .argv = NULL,
        .ret = -1,
        .rc = -1,
    };
    if (!elf_run_request_via_job(&req)) {
        return false;
    }
    ret = req.ret;
    rc = req.rc;
#else
    ret = m_elf_run_file(path, 0, NULL, &rc);
#endif

    if (ret != 0) {
        ESP_LOGW(TAG, "m_elf_run_file(%s) failed errno=%d", path, ret);
        return false;
    }
    ESP_LOGI(TAG, "applet %s rc=%d", path, rc);
    return (rc == 0);
}
#endif

void m_elf_selftests_run(void)
{
    bool ok = true;
    ESP_LOGI(TAG, "ELF selftests start");

    ok &= test_invalid_magic();
    ELF_TEST_ASSERT(ok, done, "invalid magic test");

    ok &= test_run_embedded();
    ELF_TEST_ASSERT(ok, done, "embedded ELF run");

#if CONFIG_MAGNOLIA_ELF_ENABLED && CONFIG_MAGNOLIA_ELF_APPLETS_SELFTESTS
    bool applet_ok = test_run_applet(CONFIG_MAGNOLIA_ELF_SELFTEST_APPLET_PATH);
    if (!applet_ok) {
        /* Backward-compat for earlier layouts and LittleFS mountpoint. */
        applet_ok = test_run_applet("/flash/bin/elftest") || applet_ok;
        applet_ok = test_run_applet("/flash/elftest") || applet_ok;
    }
    ok &= applet_ok;
    ELF_TEST_ASSERT(ok, done, "elf applet run");
#endif

done:
    ESP_LOGI(TAG, "ELF selftests %s", ok ? "PASS" : "FAIL");
}

#endif /* CONFIG_MAGNOLIA_ELF_ENABLED && CONFIG_MAGNOLIA_ELF_SELFTESTS */
