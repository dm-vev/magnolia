#include "sdkconfig.h"
#include <string.h>

#if CONFIG_MAGNOLIA_VFS_SELFTESTS

#include "esp_log.h"
#include "kernel/core/vfs/cache/m_vfs_read_cache.h"
#include "kernel/core/vfs/core/m_vfs_errno.h"
#include "kernel/core/vfs/core/m_vfs_selftests.h"
#include "kernel/core/vfs/m_vfs.h"
#include "kernel/core/vfs/path/m_vfs_path.h"
#include "kernel/core/vfs/core/m_vfs_object.h"
#include "kernel/core/vfs/fd/m_vfs_fd.h"

#if CONFIG_MAGNOLIA_JOB_ENABLED
#include "kernel/core/job/m_job_queue.h"
#include "kernel/core/job/m_job_result.h"
#include "kernel/core/job/m_job_wait.h"
#endif

static const char *TAG = "vfs_selftests";

static bool
report_result(const char *name, bool success)
{
    ESP_LOGI(TAG, "[%s] %s", success ? "PASS" : "FAIL", name);
    return success;
}

static bool
test_path_normalize(void)
{
    char normalized[M_VFS_PATH_MAX_LEN];
    bool ok = m_vfs_path_normalize("//tests/.././tests///tmp/.././",
                                   normalized,
                                   sizeof(normalized));
    ok &= (strcmp(normalized, "/tests") == 0);
    return report_result("path_normalize", ok);
}

static bool
test_path_resolve(void)
{
    m_vfs_error_t mount_err = m_vfs_mount("/tests", "ramfs", NULL);
    bool mounted = (mount_err == M_VFS_ERR_OK || mount_err == M_VFS_ERR_BUSY);
    m_vfs_path_t parsed;
    char buffer[M_VFS_PATH_MAX_LEN];
    m_vfs_path_normalize("/tests/./../tests", buffer, sizeof(buffer));
    bool ok = m_vfs_path_parse(buffer, &parsed);
    if (ok) {
        m_vfs_node_t *node = NULL;
        m_vfs_error_t err = m_vfs_path_resolve(NULL, &parsed, &node);
        ok &= (err == M_VFS_ERR_OK && node != NULL);
        if (node != NULL) {
            m_vfs_node_release(node);
        }
    }
    m_vfs_error_t unmount_err = m_vfs_unmount("/tests");
    ok &= (unmount_err == M_VFS_ERR_OK || unmount_err == M_VFS_ERR_NOT_FOUND);
    return report_result("path_resolve", ok && mounted);
}

static bool
test_errno_counters(void)
{
    size_t before[M_VFS_ERRNO_COUNT] = {0};
    size_t after[M_VFS_ERRNO_COUNT] = {0};
    m_vfs_errno_snapshot(before, M_VFS_ERRNO_COUNT);
    int fd = -1;
    m_vfs_error_t err = m_vfs_open(NULL, "/missing", 0, &fd);
    m_vfs_errno_snapshot(after, M_VFS_ERRNO_COUNT);
    bool ok = (err == M_VFS_ERR_NOT_FOUND);
    ok &= (after[M_ENOENT] > before[M_ENOENT]);
    return report_result("errno_counters", ok);
}

static bool
test_fd_dup_semantics(void)
{
    bool ok = (m_vfs_mount("/dup", "ramfs", NULL) == M_VFS_ERR_OK);
    int fd = -1;
    if (ok) {
        ok &= (m_vfs_open(NULL, "/dup", 0, &fd) == M_VFS_ERR_OK);
    }

    int dup_fd = -1;
    if (ok) {
        ok &= (m_vfs_dup(NULL, fd, &dup_fd) == M_VFS_ERR_OK);
        ok &= (dup_fd != fd);
    }

    int newfd = 5;
    if (newfd == fd || newfd == dup_fd) {
        newfd = 6;
    }
    if (ok) {
        ok &= (m_vfs_dup2(NULL, fd, newfd) == M_VFS_ERR_OK);
    }

    if (fd >= 0) {
        m_vfs_close(NULL, fd);
    }
    if (dup_fd >= 0) {
        m_vfs_close(NULL, dup_fd);
    }
    if (newfd >= 0) {
        m_vfs_close(NULL, newfd);
    }

    m_vfs_unmount("/dup");
    return report_result("fd_dup", ok);
}

static bool
test_stat_metadata(void)
{
    m_vfs_error_t mount_err = m_vfs_mount("/stat", "ramfs", NULL);
    bool ok = (mount_err == M_VFS_ERR_OK || mount_err == M_VFS_ERR_BUSY);
    int fd = -1;
    if (ok) {
        ok &= (m_vfs_open(NULL, "/stat", 0, &fd) == M_VFS_ERR_OK);
    }

    if (ok && fd >= 0) {
        m_vfs_stat_t stat = {0};
        m_vfs_file_t *file = m_vfs_fd_lookup(NULL, fd);
        if (file != NULL && file->node != NULL) {
            m_vfs_node_t *node = file->node;
            ok &= (node->fs_type != NULL &&
                   node->fs_type->ops != NULL &&
                   node->fs_type->ops->getattr != NULL);
            if (ok) {
                ok &= (node->fs_type->ops->getattr(node, &stat) == M_VFS_ERR_OK);
                ok &= (stat.mode != 0);
                ok &= (stat.type == M_VFS_NODE_TYPE_DIRECTORY);
            }
        } else {
            ok = false;
        }
    }

    if (fd >= 0) {
        m_vfs_close(NULL, fd);
    }
    m_vfs_unmount("/stat");
    return report_result("stat_metadata", ok);
}

static const m_vfs_fs_type_t s_selftest_cache_fs_type = {
    .name = "vfs_selftest_cache",
    .ops = NULL,
};

static size_t s_selftest_read_cache_driver_calls;

static m_vfs_error_t
_selftest_read_cache_driver(m_vfs_file_t *file,
                            void *buffer,
                            size_t size,
                            size_t *read)
{
    (void)file;
    if (buffer == NULL || read == NULL) {
        return M_VFS_ERR_INVALID_PARAM;
    }

    memset(buffer, 0xA5, size);
    *read = size;
    ++s_selftest_read_cache_driver_calls;
    return M_VFS_ERR_OK;
}

static bool
test_read_cache_concurrent(void)
{
    if (!m_vfs_read_cache_enabled()) {
        return report_result("read_cache_concurrent", true);
    }

    m_vfs_read_cache_flush_all();
    m_vfs_node_t fake_node = {
        .fs_type = &s_selftest_cache_fs_type,
    };
    m_vfs_file_t file = {0};
    file.node = &fake_node;

    s_selftest_read_cache_driver_calls = 0;
    uint8_t buffer[16] = {0};
    size_t read = 0;
    bool ok = true;

    if (m_vfs_read_cache_read(&file,
                              buffer,
                              sizeof(buffer),
                              &read,
                              _selftest_read_cache_driver) != M_VFS_ERR_OK) {
        ok = false;
    } else {
        ok &= (read == sizeof(buffer));
        ok &= (s_selftest_read_cache_driver_calls == 1);
    }

    if (m_vfs_read_cache_read(&file,
                              buffer,
                              sizeof(buffer),
                              &read,
                              _selftest_read_cache_driver) != M_VFS_ERR_OK) {
        ok = false;
    } else {
        ok &= (read == sizeof(buffer));
        ok &= (s_selftest_read_cache_driver_calls == 1);
    }

    m_vfs_read_cache_flush_file(&file);

    if (m_vfs_read_cache_read(&file,
                              buffer,
                              sizeof(buffer),
                              &read,
                              _selftest_read_cache_driver) != M_VFS_ERR_OK) {
        ok = false;
    } else {
        ok &= (read == sizeof(buffer));
        ok &= (s_selftest_read_cache_driver_calls == 2);
    }

    return report_result("read_cache_concurrent", ok);
}

static bool
test_read_cache_stats(void)
{
    if (!m_vfs_read_cache_enabled()) {
        return report_result("read_cache_disabled", true);
    }

    m_vfs_read_cache_stats_t stats = {0};
    m_vfs_read_cache_stats(&stats);
    bool ok = (stats.entries > 0 && stats.block_size > 0);
    return report_result("read_cache_stats", ok);
}

typedef struct {
    const char *dir;
    bool success;
} _m_vfs_selftests_job_ctx_t;

#if CONFIG_MAGNOLIA_JOB_ENABLED

static m_job_result_descriptor_t
_job_isolation_handler(m_job_id_t job, void *data)
{
    _m_vfs_selftests_job_ctx_t *ctx = (_m_vfs_selftests_job_ctx_t *)data;
    if (ctx == NULL) {
        return m_job_result_error(NULL, 0);
    }

    m_vfs_error_t err = m_vfs_chdir(job, ctx->dir);
    if (err != M_VFS_ERR_OK) {
        ctx->success = false;
        return m_job_result_error(NULL, 0);
    }

    char cwd[M_VFS_PATH_MAX_LEN] = {0};
    err = m_vfs_getcwd(job, cwd, sizeof(cwd));
    ctx->success = (err == M_VFS_ERR_OK && strcmp(cwd, ctx->dir) == 0);
    return m_job_result_success(NULL, 0);
}

static bool
test_job_isolation(void)
{
    bool ok = true;
    m_vfs_error_t mount_err = m_vfs_mount("/jobs", "ramfs", NULL);
    ok &= (mount_err == M_VFS_ERR_OK || mount_err == M_VFS_ERR_BUSY);
    m_vfs_error_t mkdir_err = m_vfs_mkdir(NULL, "/jobs/alpha", M_VFS_DIRECTORY_MODE_DEFAULT);
    ok &= (mkdir_err == M_VFS_ERR_OK || mkdir_err == M_VFS_ERR_BUSY);
    mkdir_err = m_vfs_mkdir(NULL, "/jobs/beta", M_VFS_DIRECTORY_MODE_DEFAULT);
    ok &= (mkdir_err == M_VFS_ERR_OK || mkdir_err == M_VFS_ERR_BUSY);

    m_job_queue_config_t config = M_JOB_QUEUE_CONFIG_DEFAULT;
    m_job_queue_t *queue = m_job_queue_create(&config);
    m_job_handle_t *handle_a = NULL;
    m_job_handle_t *handle_b = NULL;
    if (queue == NULL) {
        ok = false;
        goto cleanup_mount;
    }

    _m_vfs_selftests_job_ctx_t ctx_a = {.dir = "/jobs/alpha"};
    _m_vfs_selftests_job_ctx_t ctx_b = {.dir = "/jobs/beta"};
    m_job_error_t submit_err = m_job_queue_submit_with_handle(queue,
                                                              _job_isolation_handler,
                                                              &ctx_a,
                                                              &handle_a);
    if (submit_err == M_JOB_OK) {
        submit_err = m_job_queue_submit_with_handle(queue,
                                                   _job_isolation_handler,
                                                   &ctx_b,
                                                   &handle_b);
    }
    if (submit_err != M_JOB_OK || handle_a == NULL || handle_b == NULL) {
        ok = false;
        goto cleanup_queue;
    }

    m_job_result_descriptor_t result = {0};
    if (m_job_wait_for_job(handle_a, &result) != M_JOB_FUTURE_WAIT_OK ||
            m_job_wait_for_job(handle_b, &result) != M_JOB_FUTURE_WAIT_OK) {
        ok = false;
    } else {
        ok &= ctx_a.success && ctx_b.success;
    }

cleanup_queue:
    if (handle_a != NULL) {
        m_job_handle_destroy(handle_a);
    }
    if (handle_b != NULL) {
        m_job_handle_destroy(handle_b);
    }
    m_job_queue_destroy(queue);
cleanup_mount:
    m_vfs_unmount("/jobs");
    return report_result("job_isolation", ok);
}

#else

static bool
test_job_isolation(void)
{
    return report_result("job_isolation", true);
}

#endif

bool
m_vfs_selftests_run(void)
{
    bool overall = true;
    overall &= test_path_normalize();
    overall &= test_path_resolve();
    overall &= test_errno_counters();
    overall &= test_fd_dup_semantics();
    overall &= test_stat_metadata();
    overall &= test_read_cache_stats();
    overall &= test_read_cache_concurrent();
    overall &= test_job_isolation();
    ESP_LOGI(TAG, "self-tests %s", overall ? "PASS" : "FAIL");
    return overall;
}

#else

#include "kernel/core/vfs/core/m_vfs_selftests.h"

bool
m_vfs_selftests_run(void)
{
    return true;
}

#endif /* CONFIG_MAGNOLIA_VFS_SELFTESTS */
