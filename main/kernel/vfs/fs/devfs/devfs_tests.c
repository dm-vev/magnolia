#include "sdkconfig.h"

#if CONFIG_MAGNOLIA_VFS_DEVFS && CONFIG_MAGNOLIA_DEVFS_SELFTESTS

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "kernel/core/timer/m_timer.h"
#include "kernel/core/vfs/m_vfs.h"
#include "kernel/vfs/fs/devfs/devfs.h"
#include "kernel/vfs/fs/devfs/devfs_diag.h"
#include "kernel/vfs/fs/devfs/devfs_ioctl.h"
#include "kernel/vfs/fs/devfs/devfs_shm.h"
#if CONFIG_MAGNOLIA_VFS_STRESS_TESTS
#include "kernel/core/vfs/core/m_vfs_test.h"
#endif

static const char *TAG = "devfs_tests";
static char s_devfs_test_last_error[128];
static bool s_devfs_tests_env_ready = false;

static void
devfs_test_set_error(const char *fmt, ...)
{
    if (fmt == NULL) {
        s_devfs_test_last_error[0] = '\0';
        return;
    }

    va_list args;
    va_start(args, fmt);
    vsnprintf(s_devfs_test_last_error,
              sizeof(s_devfs_test_last_error),
              fmt,
              args);
    va_end(args);
}

#define DEVFS_TEST_ASSERT(cond, label, fmt, ...) \
    do { \
        if (!(cond)) { \
            devfs_test_set_error(fmt, ##__VA_ARGS__); \
            ok = false; \
            goto label; \
        } \
    } while (0)

static bool
test_report(const char *name, bool success)
{
    if (success) {
        ESP_LOGI(TAG, "[PASS] %s", name);
    } else {
        const char *reason = (s_devfs_test_last_error[0] != '\0')
                                 ? s_devfs_test_last_error
                                 : "unknown failure";
        ESP_LOGE(TAG, "[FAIL] %s (%s)", name, reason);
    }
    s_devfs_test_last_error[0] = '\0';
    return success;
}

static bool
devfs_tests_prepare_env(const char *context)
{
    m_vfs_error_t err = m_vfs_init();
    if (err != M_VFS_ERR_OK) {
        devfs_test_set_error("%s: m_vfs_init err=%d", context, err);
        return false;
    }

    if (s_devfs_tests_env_ready) {
        return true;
    }

    err = m_vfs_mount("/dev", "devfs", NULL);
    if (err != M_VFS_ERR_OK) {
        devfs_test_set_error("%s: m_vfs_mount err=%d", context, err);
        return false;
    }

    s_devfs_tests_env_ready = true;
    return true;
}

static void
devfs_tests_cleanup_env(void)
{
    if (!s_devfs_tests_env_ready) {
        return;
    }
    m_vfs_unmount("/dev");
    s_devfs_tests_env_ready = false;
}

static const char *const DEVFS_TEST_BLOCKING_PATH = "/dev/tests/unregister-wait";
static const char *const DEVFS_TEST_NAMESPACE_A = "/dev/tests/nested/a";
static const char *const DEVFS_TEST_NAMESPACE_B = "/dev/tests/nested/sub/b";

static m_vfs_error_t
devfs_test_blocking_read(void *private_data,
                         void *buffer,
                         size_t size,
                         size_t *read)
{
    (void)private_data;
    (void)buffer;
    if (read == NULL) {
        return M_VFS_ERR_INVALID_PARAM;
    }
    (void)size;
    *read = 0;
    return M_VFS_ERR_WOULD_BLOCK;
}

static const devfs_ops_t s_devfs_test_blocking_ops = {
    .read = devfs_test_blocking_read,
};

static m_vfs_error_t
devfs_test_passthrough_read(void *private_data,
                            void *buffer,
                            size_t size,
                            size_t *read)
{
    (void)private_data;
    (void)buffer;
    if (read == NULL) {
        return M_VFS_ERR_INVALID_PARAM;
    }
    (void)size;
    *read = 0;
    return M_VFS_ERR_OK;
}

static m_vfs_error_t
devfs_test_passthrough_write(void *private_data,
                             const void *buffer,
                             size_t size,
                             size_t *written)
{
    (void)private_data;
    (void)buffer;
    if (written == NULL) {
        return M_VFS_ERR_INVALID_PARAM;
    }
    *written = size;
    return M_VFS_ERR_OK;
}

static const devfs_ops_t s_devfs_test_passthrough_ops = {
    .read = devfs_test_passthrough_read,
    .write = devfs_test_passthrough_write,
};

typedef struct {
    int fd;
    SemaphoreHandle_t done;
    m_vfs_error_t result;
} devfs_unregister_wait_ctx_t;

static void
devfs_unregister_wait_task(void *arg)
{
    devfs_unregister_wait_ctx_t *ctx = (devfs_unregister_wait_ctx_t *)arg;
    if (ctx == NULL) {
        vTaskDelete(NULL);
        return;
    }

    uint8_t tmp = 0;
    size_t read = 0;
    ctx->result = m_vfs_read(NULL, ctx->fd, &tmp, sizeof(tmp), &read);
    if (ctx->done != NULL) {
        xSemaphoreGive(ctx->done);
    }
    vTaskDelete(NULL);
}

static const char *const DEVFS_TEST_EXTENDED_PATH = "/dev/tests/extended";
static const char *const DEVFS_TEST_FALLBACK_PATH = "/dev/tests/fallback";
static const char *const DEVFS_TEST_DIAG_WAIT_PATH = "/dev/tests/diag-wait";

typedef struct {
    bool flush_called;
    bool reset_called;
    bool destroy_called;
} devfs_extended_ctx_t;

static devfs_extended_ctx_t g_devfs_extended_ctx;

static uint32_t
devfs_extended_poll(void *private_data)
{
    (void)private_data;
    return DEVFS_EVENT_READABLE;
}

static m_vfs_error_t
devfs_extended_flush(void *private_data)
{
    (void)private_data;
    g_devfs_extended_ctx.flush_called = true;
    return M_VFS_ERR_OK;
}

static m_vfs_error_t
devfs_extended_reset(void *private_data)
{
    (void)private_data;
    g_devfs_extended_ctx.reset_called = true;
    return M_VFS_ERR_OK;
}

static m_vfs_error_t
devfs_extended_get_info(void *private_data,
                        devfs_device_info_t *info)
{
    (void)private_data;
    if (info == NULL) {
        return M_VFS_ERR_INVALID_PARAM;
    }

    info->ready_mask = DEVFS_EVENT_READABLE;
    info->notify_count = 42;
    info->poll_count = 7;
    info->blocked_count = 0;
    info->waiter_count = 1;
    info->shm_used = 0;
    info->shm_capacity = 0;
    info->unregister_events = 0;
    strncpy(info->name, "extended", sizeof(info->name));
    info->name[sizeof(info->name) - 1] = '\0';
    return M_VFS_ERR_OK;
}

static void
devfs_extended_destroy(void *private_data)
{
    (void)private_data;
    g_devfs_extended_ctx.destroy_called = true;
}

static const devfs_ops_t s_devfs_extended_ops = {
    .read = devfs_test_passthrough_read,
    .write = devfs_test_passthrough_write,
    .poll = devfs_extended_poll,
    .flush = devfs_extended_flush,
    .reset = devfs_extended_reset,
    .get_info = devfs_extended_get_info,
    .destroy = devfs_extended_destroy,
};

static bool
devfs_diag_waiter_match_cb(const devfs_diag_waiter_info_t *info,
                           void *user_data)
{
    if (info == NULL || user_data == NULL) {
        return true;
    }

    bool *found = (bool *)user_data;
    if (info->waiter_count > 0 &&
            strcmp(info->path, DEVFS_TEST_DIAG_WAIT_PATH) == 0) {
        *found = true;
        return false;
    }
    return true;
}

typedef struct {
    bool saw_directory;
    bool saw_device;
} devfs_diag_tree_test_ctx_t;

static bool
devfs_diag_tree_test_cb(const m_vfs_node_t *node, void *user_data)
{
    if (node == NULL || user_data == NULL) {
        return true;
    }

    devfs_diag_tree_test_ctx_t *ctx = (devfs_diag_tree_test_ctx_t *)user_data;
    if (node->type == M_VFS_NODE_TYPE_DIRECTORY) {
        ctx->saw_directory = true;
    }
    if (node->type == M_VFS_NODE_TYPE_DEVICE) {
        ctx->saw_device = true;
    }
    return !(ctx->saw_directory && ctx->saw_device);
}

static bool
devfs_diag_shm_capacity_cb(const devfs_diag_shm_info_t *info,
                           void *user_data)
{
    if (info == NULL || user_data == NULL) {
        return true;
    }

    bool *found = (bool *)user_data;
    if (info->capacity > 0) {
        *found = true;
        return false;
    }
    return true;
}

static bool
run_test_device_io(void)
{
    if (!devfs_tests_prepare_env("devfs device io")) {
        return false;
    }

    bool ok = true;
    uint8_t buf[16];
    int fd = -1;

    if (m_vfs_open(NULL, "/dev/null", 0, &fd) == M_VFS_ERR_OK) {
        size_t read = 0;
        ok &= (m_vfs_read(NULL, fd, buf, sizeof(buf), &read) == M_VFS_ERR_OK);
        ok &= (read == 0);
        size_t written = 0;
        ok &= (m_vfs_write(NULL, fd, buf, sizeof(buf), &written) == M_VFS_ERR_OK);
        ok &= (written == sizeof(buf));
        m_vfs_close(NULL, fd);
    } else {
        ok = false;
    }

    if (m_vfs_open(NULL, "/dev/zero", 0, &fd) == M_VFS_ERR_OK) {
        size_t read = 0;
        ok &= (m_vfs_read(NULL, fd, buf, sizeof(buf), &read) == M_VFS_ERR_OK);
        ok &= (read == sizeof(buf));
        for (size_t i = 0; i < read; ++i) {
            ok &= (buf[i] == 0);
        }
        size_t written = 0;
        ok &= (m_vfs_write(NULL, fd, buf, read, &written) == M_VFS_ERR_OK);
        ok &= (written == read);
        m_vfs_close(NULL, fd);
    } else {
        ok = false;
    }

    if (m_vfs_open(NULL, "/dev/random", 0, &fd) == M_VFS_ERR_OK) {
        size_t read = 0;
        ok &= (m_vfs_read(NULL, fd, buf, sizeof(buf), &read) == M_VFS_ERR_OK);
        ok &= (read == sizeof(buf));
        m_vfs_close(NULL, fd);
    } else {
        ok = false;
    }

    int dir_fd = -1;
    if (m_vfs_open(NULL, "/dev", 0, &dir_fd) == M_VFS_ERR_OK) {
        bool found_null = false;
        bool found_zero = false;
        bool found_random = false;
        bool dir_err = true;
        m_vfs_dirent_t entries[8];
        while (true) {
            size_t populated = 0;
            m_vfs_error_t err = m_vfs_readdir(NULL,
                                              dir_fd,
                                              entries,
                                              sizeof(entries) / sizeof(entries[0]),
                                              &populated);
            if (err != M_VFS_ERR_OK) {
                dir_err = false;
                break;
            }
            if (populated == 0) {
                break;
            }
            for (size_t i = 0; i < populated; ++i) {
                if (strcmp(entries[i].name, "null") == 0) {
                    found_null = true;
                }
                if (strcmp(entries[i].name, "zero") == 0) {
                    found_zero = true;
                }
                if (strcmp(entries[i].name, "random") == 0) {
                    found_random = true;
                }
            }
        }
        ok &= (dir_err && found_null && found_zero && found_random);
        m_vfs_close(NULL, dir_fd);
    } else {
        ok = false;
    }

    return ok;
}

static bool
run_test_poll(void)
{
    if (!devfs_tests_prepare_env("devfs poll")) {
        return false;
    }

    bool ok = true;
    int fd = -1;

    if (m_vfs_open(NULL, "/dev/null", 0, &fd) == M_VFS_ERR_OK) {
        m_vfs_pollfd_t poll_fd = {
            .fd = fd,
            .events = M_VFS_POLLIN | M_VFS_POLLOUT,
        };
        size_t ready = 0;
        ok &= (m_vfs_poll(NULL, &poll_fd, 1, NULL, &ready) == M_VFS_ERR_OK);
        ok &= (ready == 1);
        ok &= ((poll_fd.revents & (M_VFS_POLLIN | M_VFS_POLLOUT)) != 0);
        m_vfs_close(NULL, fd);
    } else {
        ok = false;
    }

    return ok;
}

static bool
run_test_devfs_unregister_wait(void)
{
    if (!devfs_tests_prepare_env("devfs unregister wait")) {
        return false;
    }

    bool ok = true;
    int reader_fd = -1;
    SemaphoreHandle_t done = NULL;
    devfs_unregister_wait_ctx_t ctx = {
        .fd = -1,
        .done = NULL,
        .result = M_VFS_ERR_INTERRUPTED,
    };

    if (devfs_register(DEVFS_TEST_BLOCKING_PATH,
                       &s_devfs_test_blocking_ops,
                       NULL) != M_VFS_ERR_OK) {
        ok = false;
        goto cleanup_unregister;
    }

    if (m_vfs_open(NULL, DEVFS_TEST_BLOCKING_PATH, 0, &reader_fd) != M_VFS_ERR_OK) {
        ok = false;
        goto cleanup_unregister;
    }

    done = xSemaphoreCreateBinary();
    if (done == NULL) {
        ok = false;
        goto cleanup_unregister;
    }

    ctx.fd = reader_fd;
    ctx.done = done;

    if (xTaskCreate(devfs_unregister_wait_task,
                    "devfs_unreg_wait",
                    2048,
                    &ctx,
                    tskIDLE_PRIORITY + 1,
                    NULL) != pdPASS) {
        ok = false;
        goto cleanup_unregister;
    }

    vTaskDelay(pdMS_TO_TICKS(10));
    ok &= (devfs_unregister(DEVFS_TEST_BLOCKING_PATH) == M_VFS_ERR_OK);
    ok &= (xSemaphoreTake(done, pdMS_TO_TICKS(1000)) == pdTRUE);
    ok &= (ctx.result == M_VFS_ERR_DESTROYED);

    int reopened_fd = -1;
    m_vfs_error_t reopen_err = m_vfs_open(NULL,
                                          DEVFS_TEST_BLOCKING_PATH,
                                          0,
                                          &reopened_fd);
    ok &= (reopen_err == M_VFS_ERR_NOT_FOUND);
    if (reopened_fd >= 0) {
        m_vfs_close(NULL, reopened_fd);
    }

cleanup_unregister:
    if (reader_fd >= 0) {
        m_vfs_close(NULL, reader_fd);
    }
    if (done != NULL) {
        vSemaphoreDelete(done);
    }
    return ok;
}

static bool
run_test_devfs_namespace(void)
{
    if (!devfs_tests_prepare_env("devfs namespace")) {
        return false;
    }

    bool ok = true;
    bool registered_a = false;
    bool registered_b = false;
    int tests_fd = -1;
    int nested_fd = -1;
    int sub_fd = -1;
    int device_a_fd = -1;
    int device_b_fd = -1;

    if (devfs_register(DEVFS_TEST_NAMESPACE_A,
                       &s_devfs_test_passthrough_ops,
                       NULL) == M_VFS_ERR_OK) {
        registered_a = true;
    } else {
        ok = false;
        goto cleanup_namespace;
    }

    if (devfs_register(DEVFS_TEST_NAMESPACE_B,
                       &s_devfs_test_passthrough_ops,
                       NULL) == M_VFS_ERR_OK) {
        registered_b = true;
    } else {
        ok = false;
        goto cleanup_namespace;
    }

    if (m_vfs_open(NULL, "/dev/tests", 0, &tests_fd) != M_VFS_ERR_OK) {
        ok = false;
        goto cleanup_namespace;
    }

    m_vfs_dirent_t entries[16];
    size_t populated = 0;
    if (m_vfs_readdir(NULL,
                      tests_fd,
                      entries,
                      sizeof(entries) / sizeof(entries[0]),
                      &populated) != M_VFS_ERR_OK) {
        ok = false;
        goto cleanup_namespace;
    }

    bool found_nested = false;
    for (size_t i = 0; i < populated; ++i) {
        if (strcmp(entries[i].name, "nested") == 0) {
            found_nested = true;
            break;
        }
    }
    ok &= found_nested;

    if (m_vfs_open(NULL, "/dev/tests/nested", 0, &nested_fd) != M_VFS_ERR_OK) {
        ok = false;
        goto cleanup_namespace;
    }

    if (m_vfs_readdir(NULL,
                      nested_fd,
                      entries,
                      sizeof(entries) / sizeof(entries[0]),
                      &populated) != M_VFS_ERR_OK) {
        ok = false;
        goto cleanup_namespace;
    }

    bool found_a = false;
    bool found_sub = false;
    for (size_t i = 0; i < populated; ++i) {
        if (strcmp(entries[i].name, "a") == 0) {
            found_a = true;
        }
        if (strcmp(entries[i].name, "sub") == 0) {
            found_sub = true;
        }
    }
    ok &= (found_a && found_sub);

    if (m_vfs_open(NULL, "/dev/tests/nested/sub", 0, &sub_fd) != M_VFS_ERR_OK) {
        ok = false;
        goto cleanup_namespace;
    }

    if (m_vfs_readdir(NULL,
                      sub_fd,
                      entries,
                      sizeof(entries) / sizeof(entries[0]),
                      &populated) != M_VFS_ERR_OK) {
        ok = false;
        goto cleanup_namespace;
    }

    bool found_b = false;
    for (size_t i = 0; i < populated; ++i) {
        if (strcmp(entries[i].name, "b") == 0) {
            found_b = true;
            break;
        }
    }
    ok &= found_b;

    if (m_vfs_open(NULL, DEVFS_TEST_NAMESPACE_A, 0, &device_a_fd) != M_VFS_ERR_OK) {
        ok = false;
        goto cleanup_namespace;
    }
    if (m_vfs_open(NULL, DEVFS_TEST_NAMESPACE_B, 0, &device_b_fd) != M_VFS_ERR_OK) {
        ok = false;
        goto cleanup_namespace;
    }

cleanup_namespace:
    if (device_a_fd >= 0) {
        m_vfs_close(NULL, device_a_fd);
    }
    if (device_b_fd >= 0) {
        m_vfs_close(NULL, device_b_fd);
    }
    if (sub_fd >= 0) {
        m_vfs_close(NULL, sub_fd);
    }
    if (nested_fd >= 0) {
        m_vfs_close(NULL, nested_fd);
    }
    if (tests_fd >= 0) {
        m_vfs_close(NULL, tests_fd);
    }
    if (registered_a) {
        devfs_unregister(DEVFS_TEST_NAMESPACE_A);
    }
    if (registered_b) {
        devfs_unregister(DEVFS_TEST_NAMESPACE_B);
    }
    return ok;
}

#if CONFIG_MAGNOLIA_DEVFS_PIPES
static bool
run_test_devfs_pipe_basic(void)
{
    if (!devfs_tests_prepare_env("devfs pipe basic")) {
        return false;
    }

    bool ok = true;
    int fd = -1;
    const char payload[] = "stream-pipe";
    uint8_t sink[32] = {0};
    size_t written = 0;
    size_t read = 0;

    if (m_vfs_open(NULL, "/dev/pipe0", 0, &fd) != M_VFS_ERR_OK) {
        ok = false;
        goto cleanup_pipe;
    }

    ok &= (m_vfs_write(NULL, fd, payload, sizeof(payload) - 1, &written) == M_VFS_ERR_OK);
    ok &= (written == sizeof(payload) - 1);
    ok &= (m_vfs_read(NULL, fd, sink, sizeof(payload) - 1, &read) == M_VFS_ERR_OK);
    ok &= (read == sizeof(payload) - 1);
    ok &= (memcmp(sink, payload, read) == 0);

cleanup_pipe:
    if (fd >= 0) {
        m_vfs_close(NULL, fd);
    }
    return ok;
}
#endif /* CONFIG_MAGNOLIA_DEVFS_PIPES */

#if CONFIG_MAGNOLIA_DEVFS_TTY
static bool
run_test_devfs_tty_canonical(void)
{
    if (!devfs_tests_prepare_env("tty canonical")) {
        return false;
    }

    bool ok = true;
    int fd = -1;
    const char payload[] = "foo\bbar\n";
    const char expected[] = "fobar\n";
    uint8_t sink[32] = {0};
    size_t written = 0;
    size_t read = 0;
    const uint8_t ctrl_d = 0x04;

    DEVFS_TEST_ASSERT(m_vfs_open(NULL, "/dev/tty0", 0, &fd) == M_VFS_ERR_OK,
                      cleanup_tty,
                      "tty canonical: open failed");

    m_vfs_error_t err = m_vfs_write(NULL,
                                    fd,
                                    payload,
                                    sizeof(payload) - 1,
                                    &written);
    DEVFS_TEST_ASSERT(err == M_VFS_ERR_OK && written == sizeof(payload) - 1,
                      cleanup_tty,
                      "tty canonical: initial write err=%d written=%u",
                      err,
                      (unsigned)written);
    err = m_vfs_read(NULL, fd, sink, sizeof(expected) - 1, &read);
    DEVFS_TEST_ASSERT(err == M_VFS_ERR_OK && read == sizeof(expected) - 1 &&
                      memcmp(sink, expected, read) == 0,
                      cleanup_tty,
                      "tty canonical: canonical read err=%d read=%u",
                      err,
                      (unsigned)read);

    err = m_vfs_write(NULL, fd, &ctrl_d, sizeof(ctrl_d), &written);
    DEVFS_TEST_ASSERT(err == M_VFS_ERR_OK && written == sizeof(ctrl_d),
                      cleanup_tty,
                      "tty canonical: ctrl-d write err=%d written=%u",
                      err,
                      (unsigned)written);
    err = m_vfs_read(NULL, fd, sink, sizeof(sink), &read);
    DEVFS_TEST_ASSERT(err == M_VFS_ERR_OK && read == 0,
                      cleanup_tty,
                      "tty canonical: ctrl-d read err=%d read=%u",
                      err,
                      (unsigned)read);

    bool canon = false;
    err = m_vfs_ioctl(NULL, fd, DEVFS_IOCTL_TTY_SET_CANON, &canon);
    DEVFS_TEST_ASSERT(err == M_VFS_ERR_OK,
                      cleanup_tty,
                      "tty canonical: ioctl set canon err=%d",
                      err);
    const char raw[] = "raw-input";
    err = m_vfs_write(NULL, fd, raw, sizeof(raw) - 1, &written);
    DEVFS_TEST_ASSERT(err == M_VFS_ERR_OK && written == sizeof(raw) - 1,
                      cleanup_tty,
                      "tty canonical: raw write err=%d written=%u",
                      err,
                      (unsigned)written);
    err = m_vfs_read(NULL, fd, sink, sizeof(raw) - 1, &read);
    DEVFS_TEST_ASSERT(err == M_VFS_ERR_OK && read == sizeof(raw) - 1 &&
                      memcmp(sink, raw, read) == 0,
                      cleanup_tty,
                      "tty canonical: raw read err=%d read=%u",
                      err,
                      (unsigned)read);

cleanup_tty:
    if (fd >= 0) {
        m_vfs_close(NULL, fd);
    }
    return ok;
}
#endif /* CONFIG_MAGNOLIA_DEVFS_TTY */

#if CONFIG_MAGNOLIA_DEVFS_PTY
static bool
run_test_devfs_pty_basic(void)
{
    if (!devfs_tests_prepare_env("pty basic")) {
        return false;
    }

    bool ok = true;
    int master_fd = -1;
    int slave_fd = -1;
    const char master_payload[] = "master->slave\n";
    const char slave_payload[] = "slave->master";
    uint8_t master_sink[32] = {0};
    uint8_t slave_sink[32] = {0};
    size_t written = 0;
    size_t read = 0;

    bool master_ok = (m_vfs_open(NULL, "/dev/pty/master0", 0, &master_fd) == M_VFS_ERR_OK);
    bool slave_ok = (m_vfs_open(NULL, "/dev/pty/slave0", 0, &slave_fd) == M_VFS_ERR_OK);
    DEVFS_TEST_ASSERT(master_ok && slave_ok,
                      cleanup_pty,
                      "pty basic: open master=%d slave=%d",
                      master_ok,
                      slave_ok);

    m_vfs_error_t err = m_vfs_write(NULL,
                                    master_fd,
                                    master_payload,
                                    sizeof(master_payload) - 1,
                                    &written);
    DEVFS_TEST_ASSERT(err == M_VFS_ERR_OK && written == sizeof(master_payload) - 1,
                      cleanup_pty,
                      "pty basic: master write err=%d written=%u",
                      err,
                      (unsigned)written);
    err = m_vfs_read(NULL,
                     slave_fd,
                     slave_sink,
                     sizeof(master_payload) - 1,
                     &read);
    DEVFS_TEST_ASSERT(err == M_VFS_ERR_OK && read == sizeof(master_payload) - 1 &&
                      memcmp(slave_sink, master_payload, read) == 0,
                      cleanup_pty,
                      "pty basic: slave read err=%d read=%u",
                      err,
                      (unsigned)read);

    err = m_vfs_write(NULL,
                      slave_fd,
                      slave_payload,
                      sizeof(slave_payload) - 1,
                      &written);
    DEVFS_TEST_ASSERT(err == M_VFS_ERR_OK && written == sizeof(slave_payload) - 1,
                      cleanup_pty,
                      "pty basic: slave write err=%d written=%u",
                      err,
                      (unsigned)written);
    err = m_vfs_read(NULL,
                     master_fd,
                     master_sink,
                     sizeof(slave_payload) - 1,
                     &read);
    DEVFS_TEST_ASSERT(err == M_VFS_ERR_OK && read == sizeof(slave_payload) - 1 &&
                      memcmp(master_sink, slave_payload, read) == 0,
                      cleanup_pty,
                      "pty basic: master read err=%d read=%u",
                      err,
                      (unsigned)read);

cleanup_pty:
    if (master_fd >= 0) {
        m_vfs_close(NULL, master_fd);
    }
    if (slave_fd >= 0) {
        m_vfs_close(NULL, slave_fd);
    }
    return ok;
}
#endif /* CONFIG_MAGNOLIA_DEVFS_PTY */

static bool
run_test_devfs_extended_ops(void)
{
    if (!devfs_tests_prepare_env("devfs extended ops")) {
        return false;
    }

    bool ok = true;
    int fd = -1;
    memset(&g_devfs_extended_ctx, 0, sizeof(g_devfs_extended_ctx));

    if (devfs_register(DEVFS_TEST_EXTENDED_PATH,
                       &s_devfs_extended_ops,
                       &g_devfs_extended_ctx) != M_VFS_ERR_OK) {
        ok = false;
        goto cleanup;
    }

    if (m_vfs_open(NULL, DEVFS_TEST_EXTENDED_PATH, 0, &fd) != M_VFS_ERR_OK) {
        ok = false;
        goto cleanup;
    }

    devfs_event_mask_t mask = 0;
    ok &= (m_vfs_ioctl(NULL, fd, DEVFS_IOCTL_POLL_MASK, &mask) == M_VFS_ERR_OK);
    ok &= (mask == DEVFS_EVENT_READABLE);
    ok &= (m_vfs_ioctl(NULL, fd, DEVFS_IOCTL_FLUSH, NULL) == M_VFS_ERR_OK);
    ok &= g_devfs_extended_ctx.flush_called;
    ok &= (m_vfs_ioctl(NULL, fd, DEVFS_IOCTL_RESET, NULL) == M_VFS_ERR_OK);
    ok &= g_devfs_extended_ctx.reset_called;

    devfs_device_info_t info = {0};
    ok &= (m_vfs_ioctl(NULL,
                       fd,
                       DEVFS_IOCTL_GET_INFO,
                       &info) == M_VFS_ERR_OK);
    ok &= (info.ready_mask == DEVFS_EVENT_READABLE);
    ok &= (info.notify_count == 42);
    ok &= (info.poll_count == 7);

    ok &= (m_vfs_ioctl(NULL, fd, DEVFS_IOCTL_DESTROY, NULL) == M_VFS_ERR_OK);
    ok &= g_devfs_extended_ctx.destroy_called;

cleanup:
    if (fd >= 0) {
        m_vfs_close(NULL, fd);
    }
    devfs_unregister(DEVFS_TEST_EXTENDED_PATH);
    return ok;
}

static bool
run_test_devfs_fallback_ops(void)
{
    if (!devfs_tests_prepare_env("devfs fallback ops")) {
        return false;
    }

    bool ok = true;
    int fd = -1;
    if (devfs_register(DEVFS_TEST_FALLBACK_PATH,
                       &s_devfs_test_passthrough_ops,
                       NULL) != M_VFS_ERR_OK) {
        ok = false;
        goto cleanup;
    }

    if (m_vfs_open(NULL, DEVFS_TEST_FALLBACK_PATH, 0, &fd) != M_VFS_ERR_OK) {
        ok = false;
        goto cleanup;
    }

    devfs_event_mask_t mask = 0;
    ok &= (m_vfs_ioctl(NULL, fd, DEVFS_IOCTL_POLL_MASK, &mask) == M_VFS_ERR_OK);
    ok &= (mask == 0);
    ok &= (m_vfs_ioctl(NULL, fd, DEVFS_IOCTL_FLUSH, NULL) == M_VFS_ERR_OK);
    ok &= (m_vfs_ioctl(NULL, fd, DEVFS_IOCTL_RESET, NULL) == M_VFS_ERR_NOT_SUPPORTED);
    devfs_device_info_t info = {0};
    ok &= (m_vfs_ioctl(NULL, fd, DEVFS_IOCTL_GET_INFO, &info) == M_VFS_ERR_NOT_SUPPORTED);
    ok &= (m_vfs_ioctl(NULL, fd, DEVFS_IOCTL_DESTROY, NULL) == M_VFS_ERR_OK);

cleanup:
    if (fd >= 0) {
        m_vfs_close(NULL, fd);
    }
    devfs_unregister(DEVFS_TEST_FALLBACK_PATH);
    return ok;
}

static bool
run_test_devfs_diag_output(void)
{
    if (!devfs_tests_prepare_env("devfs diagnostics")) {
        return false;
    }

    bool ok = true;
    int reader_fd = -1;
    SemaphoreHandle_t done = NULL;
    devfs_unregister_wait_ctx_t ctx = {
        .fd = -1,
        .done = NULL,
        .result = M_VFS_ERR_INTERRUPTED,
    };
    bool unregistered = false;

    if (devfs_register(DEVFS_TEST_DIAG_WAIT_PATH,
                       &s_devfs_test_blocking_ops,
                       NULL) != M_VFS_ERR_OK) {
        ok = false;
        goto cleanup;
    }

    if (m_vfs_open(NULL, DEVFS_TEST_DIAG_WAIT_PATH, 0, &reader_fd) != M_VFS_ERR_OK) {
        ok = false;
        goto cleanup;
    }

    done = xSemaphoreCreateBinary();
    if (done == NULL) {
        ok = false;
        goto cleanup;
    }

    ctx.fd = reader_fd;
    ctx.done = done;
    if (xTaskCreate(devfs_unregister_wait_task,
                    "devfs_diag_wait",
                    2048,
                    &ctx,
                    tskIDLE_PRIORITY + 1,
                    NULL) != pdPASS) {
        ok = false;
        goto cleanup;
    }

    vTaskDelay(pdMS_TO_TICKS(10));

    bool waiter_seen = false;
    devfs_diag_waiters(devfs_diag_waiter_match_cb, &waiter_seen);
    ok &= waiter_seen;

    devfs_diag_tree_test_ctx_t tree_ctx = {false, false};
    devfs_diag_tree_snapshot(devfs_diag_tree_test_cb, &tree_ctx);
    ok &= (tree_ctx.saw_directory && tree_ctx.saw_device);

    bool shm_seen = false;
#if CONFIG_MAGNOLIA_IPC_ENABLED
    devfs_diag_shm_info(devfs_diag_shm_capacity_cb, &shm_seen);
    ok &= shm_seen;
#else
    (void)shm_seen;
#endif

    int poll_fd = -1;
    if (m_vfs_open(NULL, "/dev/null", 0, &poll_fd) == M_VFS_ERR_OK) {
        m_vfs_pollfd_t poll_entry = {
            .fd = poll_fd,
            .events = M_VFS_POLLIN,
        };
        size_t ready = 0;
        m_vfs_poll(NULL, &poll_entry, 1, NULL, &ready);
        m_vfs_close(NULL, poll_fd);
    }
    ok &= (devfs_diag_total_poll_count() > 0);

    size_t before_unreg = devfs_diag_unregister_events();
    if (devfs_unregister(DEVFS_TEST_DIAG_WAIT_PATH) == M_VFS_ERR_OK) {
        unregistered = true;
    }
    size_t after_unreg = devfs_diag_unregister_events();
    if (unregistered) {
        ok &= (after_unreg == before_unreg + 1);
    } else {
        ok &= (after_unreg == before_unreg);
    }

    ok &= (xSemaphoreTake(done, pdMS_TO_TICKS(1000)) == pdTRUE);
    ok &= (ctx.result == M_VFS_ERR_DESTROYED);

cleanup:
    if (reader_fd >= 0) {
        m_vfs_close(NULL, reader_fd);
    }
    if (done != NULL) {
        vSemaphoreDelete(done);
    }
    if (!unregistered) {
        devfs_unregister(DEVFS_TEST_DIAG_WAIT_PATH);
    }
    return ok;
}



#if CONFIG_MAGNOLIA_IPC_ENABLED

typedef struct {
    int fd;
    const uint8_t *payload;
    size_t length;
    TickType_t delay;
    SemaphoreHandle_t done;
    bool success;
} devfs_shm_writer_ctx_t;

static void
devfs_shm_writer_task(void *arg)
{
    devfs_shm_writer_ctx_t *ctx = (devfs_shm_writer_ctx_t *)arg;
    if (ctx == NULL) {
        vTaskDelete(NULL);
        return;
    }

    if (ctx->delay > 0) {
        vTaskDelay(ctx->delay);
    }

    size_t written = 0;
    m_vfs_error_t err = m_vfs_write(NULL,
                                    ctx->fd,
                                    ctx->payload,
                                    ctx->length,
                                    &written);
    ctx->success = (err == M_VFS_ERR_OK && written == ctx->length);
    if (ctx->done != NULL) {
        xSemaphoreGive(ctx->done);
    }
    vTaskDelete(NULL);
}

typedef struct {
    int fd;
    SemaphoreHandle_t done;
    m_vfs_error_t result;
} devfs_shm_waiter_ctx_t;

static void
devfs_shm_reader_wait_task(void *arg)
{
    devfs_shm_waiter_ctx_t *ctx = (devfs_shm_waiter_ctx_t *)arg;
    if (ctx == NULL) {
        vTaskDelete(NULL);
        return;
    }

    uint8_t tmp = 0;
    size_t read = 0;
    ctx->result = m_vfs_read(NULL, ctx->fd, &tmp, 1, &read);
    if (ctx->done != NULL) {
        xSemaphoreGive(ctx->done);
    }
    vTaskDelete(NULL);
}

#endif /* CONFIG_MAGNOLIA_IPC_ENABLED */

#if CONFIG_MAGNOLIA_DEVFS_PIPES

typedef struct {
    int fd;
    SemaphoreHandle_t done;
    m_vfs_error_t result;
} devfs_pipe_waiter_ctx_t;

static void
devfs_pipe_reader_wait_task(void *arg)
{
    devfs_pipe_waiter_ctx_t *ctx = (devfs_pipe_waiter_ctx_t *)arg;
    if (ctx == NULL) {
        vTaskDelete(NULL);
        return;
    }

    uint8_t tmp = 0;
    size_t read = 0;
    ctx->result = m_vfs_read(NULL, ctx->fd, &tmp, 1, &read);
    if (ctx->done != NULL) {
        xSemaphoreGive(ctx->done);
    }
    vTaskDelete(NULL);
}

typedef struct {
    int fd;
    SemaphoreHandle_t done;
    m_vfs_error_t result;
    uint32_t revents;
    size_t ready;
} devfs_poll_waiter_ctx_t;

static void
devfs_poll_wait_task(void *arg)
{
    devfs_poll_waiter_ctx_t *ctx = (devfs_poll_waiter_ctx_t *)arg;
    if (ctx == NULL) {
        vTaskDelete(NULL);
        return;
    }

    m_vfs_pollfd_t entry = {
        .fd = ctx->fd,
        .events = M_VFS_POLLIN,
    };
    m_timer_deadline_t deadline = m_timer_deadline_from_relative(5000000);
    ctx->result = m_vfs_poll(NULL, &entry, 1, &deadline, &ctx->ready);
    ctx->revents = entry.revents;

    if (ctx->done != NULL) {
        xSemaphoreGive(ctx->done);
    }
    vTaskDelete(NULL);
}

typedef struct {
    int fd;
    SemaphoreHandle_t done;
    m_vfs_error_t result;
} devfs_pipe_writer_ctx_t;

static void
devfs_pipe_writer_wait_task(void *arg)
{
    devfs_pipe_writer_ctx_t *ctx = (devfs_pipe_writer_ctx_t *)arg;
    if (ctx == NULL) {
        vTaskDelete(NULL);
        return;
    }

    uint8_t payload[512];
    memset(payload, 0xCD, sizeof(payload));
    size_t written = 0;
    ctx->result = m_vfs_write(NULL,
                               ctx->fd,
                               payload,
                               sizeof(payload),
                               &written);
    if (ctx->done != NULL) {
        xSemaphoreGive(ctx->done);
    }
    vTaskDelete(NULL);
}

static bool
run_test_pipe_close_wakes_reader(void)
{
    if (!devfs_tests_prepare_env("pipe close wake")) {
        return false;
    }

    bool ok = true;
    int reader_fd = -1;
    DEVFS_TEST_ASSERT(m_vfs_open(NULL, "/dev/pipe0", 0, &reader_fd) == M_VFS_ERR_OK,
                      cleanup,
                      "pipe close: open reader failed");

    SemaphoreHandle_t done = xSemaphoreCreateBinary();
    DEVFS_TEST_ASSERT(done != NULL, cleanup, "pipe close: semaphore alloc failed");

    devfs_pipe_waiter_ctx_t ctx = {
        .fd = reader_fd,
        .done = done,
        .result = M_VFS_ERR_OK,
    };

    if (xTaskCreate(devfs_pipe_reader_wait_task,
                    "devfs_pipe_wait_reader",
                    2048,
                    &ctx,
                    tskIDLE_PRIORITY + 1,
                    NULL) != pdPASS) {
        ESP_LOGE(TAG, "pipe close: reader task create failed");
        vSemaphoreDelete(done);
        ok = false;
        goto cleanup;
    }

    vTaskDelay(pdMS_TO_TICKS(10));
    m_vfs_close(NULL, reader_fd);
    reader_fd = -1;

    DEVFS_TEST_ASSERT(xSemaphoreTake(done, pdMS_TO_TICKS(1000)) == pdTRUE,
                      cleanup,
                      "pipe close: reader completion timeout");
    vSemaphoreDelete(done);
    DEVFS_TEST_ASSERT(ctx.result == M_VFS_ERR_DESTROYED,
                      cleanup,
                      "pipe close: reader err=%d expected=%d",
                      ctx.result,
                      M_VFS_ERR_DESTROYED);

cleanup:
    if (reader_fd >= 0) {
        m_vfs_close(NULL, reader_fd);
    }
    return ok;
}

static bool
run_test_pipe_poll_close_wakes_waiter(void)
{
    if (!devfs_tests_prepare_env("pipe poll close wake")) {
        return false;
    }

    bool ok = true;
    int fd = -1;
    DEVFS_TEST_ASSERT(m_vfs_open(NULL, "/dev/pipe0", 0, &fd) == M_VFS_ERR_OK,
                      cleanup,
                      "pipe poll close: open failed");

    SemaphoreHandle_t done = xSemaphoreCreateBinary();
    DEVFS_TEST_ASSERT(done != NULL, cleanup, "pipe poll close: semaphore alloc failed");

    devfs_poll_waiter_ctx_t ctx = {
        .fd = fd,
        .done = done,
        .result = M_VFS_ERR_OK,
        .revents = 0,
        .ready = 0,
    };

    if (xTaskCreate(devfs_poll_wait_task,
                    "devfs_pipe_poll_wait",
                    2048,
                    &ctx,
                    tskIDLE_PRIORITY + 1,
                    NULL) != pdPASS) {
        ESP_LOGE(TAG, "pipe poll close: poll task create failed");
        vSemaphoreDelete(done);
        ok = false;
        goto cleanup;
    }

    vTaskDelay(pdMS_TO_TICKS(10));
    m_vfs_close(NULL, fd);
    fd = -1;

    DEVFS_TEST_ASSERT(xSemaphoreTake(done, pdMS_TO_TICKS(1000)) == pdTRUE,
                      cleanup,
                      "pipe poll close: poll completion timeout");
    vSemaphoreDelete(done);
    DEVFS_TEST_ASSERT((ctx.result == M_VFS_ERR_DESTROYED) ||
                      (ctx.result == M_VFS_ERR_OK &&
                       ctx.ready == 1 &&
                       (ctx.revents & (M_VFS_POLLERR | M_VFS_POLLHUP)) != 0),
                      cleanup,
                      "pipe poll close: poll err=%d ready=%u revents=0x%x",
                      ctx.result,
                      (unsigned)ctx.ready,
                      (unsigned)ctx.revents);

cleanup:
    if (fd >= 0) {
        m_vfs_close(NULL, fd);
    }
    return ok;
}

static bool
run_test_pipe_close_wakes_blocked_writer(void)
{
    if (!devfs_tests_prepare_env("pipe writer close wake")) {
        return false;
    }

    bool ok = true;
    int fd = -1;
    DEVFS_TEST_ASSERT(m_vfs_open(NULL, "/dev/pipe0", 0, &fd) == M_VFS_ERR_OK,
                      cleanup,
                      "pipe writer close: open failed");

    SemaphoreHandle_t done = xSemaphoreCreateBinary();
    DEVFS_TEST_ASSERT(done != NULL, cleanup, "pipe writer close: semaphore alloc failed");

    devfs_pipe_writer_ctx_t ctx = {
        .fd = fd,
        .done = done,
        .result = M_VFS_ERR_OK,
    };

    if (xTaskCreate(devfs_pipe_writer_wait_task,
                    "devfs_pipe_write_wait",
                    2048,
                    &ctx,
                    tskIDLE_PRIORITY + 1,
                    NULL) != pdPASS) {
        ESP_LOGE(TAG, "pipe writer close: writer task create failed");
        vSemaphoreDelete(done);
        ok = false;
        goto cleanup;
    }

    vTaskDelay(pdMS_TO_TICKS(10));
    m_vfs_close(NULL, fd);
    fd = -1;

    DEVFS_TEST_ASSERT(xSemaphoreTake(done, pdMS_TO_TICKS(1000)) == pdTRUE,
                      cleanup,
                      "pipe writer close: writer completion timeout");
    vSemaphoreDelete(done);
    DEVFS_TEST_ASSERT(ctx.result == M_VFS_ERR_DESTROYED,
                      cleanup,
                      "pipe writer close: writer err=%d expected=%d",
                      ctx.result,
                      M_VFS_ERR_DESTROYED);

cleanup:
    if (fd >= 0) {
        m_vfs_close(NULL, fd);
    }
    return ok;
}

#endif /* CONFIG_MAGNOLIA_DEVFS_PIPES */

#if CONFIG_MAGNOLIA_IPC_ENABLED

static bool
run_test_shm_pipe_concurrent(void)
{
    if (!devfs_tests_prepare_env("shm concurrent")) {
        return false;
    }

    bool ok = true;
    int reader_fd = -1;
    int writer_fd = -1;
    const uint8_t payload[] = "shmpipe";

    DEVFS_TEST_ASSERT(m_vfs_open(NULL, "/dev/pipe0", 0, &reader_fd) == M_VFS_ERR_OK,
                      cleanup,
                      "shm concurrent: open reader failed");

    DEVFS_TEST_ASSERT(m_vfs_open(NULL, "/dev/pipe0", 0, &writer_fd) == M_VFS_ERR_OK,
                      cleanup,
                      "shm concurrent: open writer failed");

    SemaphoreHandle_t done = xSemaphoreCreateBinary();
    DEVFS_TEST_ASSERT(done != NULL, cleanup, "shm concurrent: semaphore alloc failed");

    devfs_shm_writer_ctx_t ctx = {
        .fd = writer_fd,
        .payload = payload,
        .length = sizeof(payload),
        .delay = 0,
        .done = done,
        .success = false,
    };

    if (xTaskCreate(devfs_shm_writer_task,
                    "devfs_shm_writer",
                    2048,
                    &ctx,
                    tskIDLE_PRIORITY + 1,
                    NULL) != pdPASS) {
        ESP_LOGE(TAG, "shm concurrent: writer task create failed");
        vSemaphoreDelete(done);
        ok = false;
        goto cleanup;
    }

    size_t read = 0;
    uint8_t buffer[16] = {0};
    m_vfs_error_t err = m_vfs_read(NULL,
                                   reader_fd,
                                   buffer,
                                   sizeof(payload),
                                   &read);
    DEVFS_TEST_ASSERT(err == M_VFS_ERR_OK && read == sizeof(payload) &&
                      memcmp(buffer, payload, read) == 0,
                      cleanup,
                      "shm concurrent: read err=%d read=%u",
                      err,
                      (unsigned)read);
    DEVFS_TEST_ASSERT(xSemaphoreTake(done, pdMS_TO_TICKS(1000)) == pdTRUE,
                      cleanup,
                      "shm concurrent: writer completion timeout");
    vSemaphoreDelete(done);
    DEVFS_TEST_ASSERT(ctx.success, cleanup, "shm concurrent: writer failed");

cleanup:
    if (reader_fd >= 0) {
        m_vfs_close(NULL, reader_fd);
    }
    if (writer_fd >= 0) {
        m_vfs_close(NULL, writer_fd);
    }
    return ok;
}

static bool
run_test_shm_pipe_close_wakes_reader(void)
{
    if (!devfs_tests_prepare_env("shm pipe close wake")) {
        return false;
    }

    bool ok = true;
    int reader_fd = -1;
    DEVFS_TEST_ASSERT(m_vfs_open(NULL, "/dev/pipe0", 0, &reader_fd) == M_VFS_ERR_OK,
                      cleanup,
                      "shm pipe close: open reader failed");

    SemaphoreHandle_t done = xSemaphoreCreateBinary();
    DEVFS_TEST_ASSERT(done != NULL, cleanup, "shm pipe close: semaphore alloc failed");

    devfs_shm_waiter_ctx_t ctx = {
        .fd = reader_fd,
        .done = done,
        .result = M_VFS_ERR_OK,
    };

    if (xTaskCreate(devfs_shm_reader_wait_task,
                    "devfs_shm_pipe_wait_reader",
                    2048,
                    &ctx,
                    tskIDLE_PRIORITY + 1,
                    NULL) != pdPASS) {
        ESP_LOGE(TAG, "shm pipe close: reader task create failed");
        vSemaphoreDelete(done);
        ok = false;
        goto cleanup;
    }

    vTaskDelay(pdMS_TO_TICKS(10));
    m_vfs_close(NULL, reader_fd);
    reader_fd = -1;

    DEVFS_TEST_ASSERT(xSemaphoreTake(done, pdMS_TO_TICKS(1000)) == pdTRUE,
                      cleanup,
                      "shm pipe close: reader completion timeout");
    vSemaphoreDelete(done);
    DEVFS_TEST_ASSERT(ctx.result == M_VFS_ERR_DESTROYED,
                      cleanup,
                      "shm pipe close: reader err=%d expected=%d",
                      ctx.result,
                      M_VFS_ERR_DESTROYED);

cleanup:
    if (reader_fd >= 0) {
        m_vfs_close(NULL, reader_fd);
    }
    return ok;
}

static bool
run_test_shm_pipe_timeout(void)
{
    if (!devfs_tests_prepare_env("shm timeout")) {
        return false;
    }

    bool ok = true;
    int fd = -1;
    DEVFS_TEST_ASSERT(m_vfs_open(NULL, "/dev/pipe0", 0, &fd) == M_VFS_ERR_OK,
                      cleanup,
                      "shm timeout: open failed");

    devfs_shm_buffer_info_t info = {0};
    m_vfs_error_t err = m_vfs_ioctl(NULL, fd, DEVFS_SHM_IOCTL_BUFFER_INFO, &info);
    DEVFS_TEST_ASSERT(err == M_VFS_ERR_OK && info.capacity > 0,
                      cleanup,
                      "shm timeout: buffer info err=%d capacity=%u",
                      err,
                      (unsigned)info.capacity);

    uint8_t chunk[32];
    memset(chunk, 0xAB, sizeof(chunk));
    size_t total_written = 0;
    size_t extra_written = 0;
    m_vfs_error_t timed_err = M_VFS_ERR_OK;

    while (true) {
        size_t written = 0;
        m_timer_deadline_t fill_deadline = m_timer_deadline_from_relative(10000);
        m_vfs_error_t write_err = m_vfs_write_timed(NULL,
                                                    fd,
                                                    chunk,
                                                    sizeof(chunk),
                                                    &written,
                                                    &fill_deadline);
        if (write_err == M_VFS_ERR_TIMEOUT && written == 0) {
            timed_err = M_VFS_ERR_TIMEOUT;
            extra_written = 0;
            break;
        }
        DEVFS_TEST_ASSERT(write_err == M_VFS_ERR_OK && written > 0,
                          cleanup,
                          "shm timeout: fill write err=%d written=%u",
                          write_err,
                          (unsigned)written);
        total_written += written;

        extra_written = 0;
        m_timer_deadline_t probe_deadline = m_timer_deadline_from_relative(1000);
        timed_err = m_vfs_write_timed(NULL,
                                      fd,
                                      "Z",
                                      1,
                                      &extra_written,
                                      &probe_deadline);
        if (timed_err == M_VFS_ERR_TIMEOUT) {
            break;
        }
        DEVFS_TEST_ASSERT(timed_err == M_VFS_ERR_OK && extra_written == 1,
                          cleanup,
                          "shm timeout: probe write err=%d written=%u",
                          timed_err,
                          (unsigned)extra_written);
        total_written += extra_written;
    }

    DEVFS_TEST_ASSERT(total_written > 0,
                      cleanup,
                      "shm timeout: fill wrote nothing");
    DEVFS_TEST_ASSERT(timed_err == M_VFS_ERR_TIMEOUT && extra_written == 0,
                      cleanup,
                      "shm timeout: timed write err=%d written=%u",
                      timed_err,
                      (unsigned)extra_written);

    uint8_t tmp = 0;
    size_t read = 0;
    m_vfs_error_t read_err = m_vfs_read(NULL, fd, &tmp, 1, &read);
    DEVFS_TEST_ASSERT(read_err == M_VFS_ERR_OK && read == 1,
                      cleanup,
                      "shm timeout: drain read err=%d read=%u",
                      read_err,
                      (unsigned)read);

    size_t final_written = 0;
    err = m_vfs_write(NULL, fd, "X", 1, &final_written);
    DEVFS_TEST_ASSERT(err == M_VFS_ERR_OK && final_written == 1,
                      cleanup,
                      "shm timeout: final write err=%d written=%u",
                      err,
                      (unsigned)final_written);

cleanup:
    if (fd >= 0) {
        m_vfs_close(NULL, fd);
    }
    return ok;
}

static bool
run_test_shm_stream_drop(void)
{
    if (!devfs_tests_prepare_env("shm drop")) {
        return false;
    }

    bool ok = true;
    int reader_fd = -1;
    int writer_fd = -1;
    DEVFS_TEST_ASSERT(m_vfs_open(NULL, "/dev/stream0", 0, &reader_fd) == M_VFS_ERR_OK,
                      cleanup,
                      "shm drop: open failed");
    DEVFS_TEST_ASSERT(m_vfs_open(NULL, "/dev/stream0", 0, &writer_fd) == M_VFS_ERR_OK,
                      cleanup,
                      "shm drop: open writer failed");

    uint8_t payload[512];
    devfs_shm_buffer_info_t info = {0};
    m_vfs_error_t err = m_vfs_ioctl(NULL, reader_fd, DEVFS_SHM_IOCTL_BUFFER_INFO, &info);
    DEVFS_TEST_ASSERT(err == M_VFS_ERR_OK &&
                      info.capacity > 0 &&
                      info.capacity * 2 <= sizeof(payload),
                      cleanup,
                      "shm drop: buffer info err=%d capacity=%u",
                      err,
                      (unsigned)info.capacity);

    size_t total = info.capacity * 2;
    for (size_t i = 0; i < total; ++i) {
        payload[i] = (uint8_t)i;
    }

    size_t total_written = 0;
    for (size_t offset = 0; offset < total;) {
        size_t chunk_size = ((total - offset) > 64) ? 64 : (total - offset);
        size_t written = 0;
        m_timer_deadline_t write_deadline = m_timer_deadline_from_relative(10000);
        m_vfs_error_t write_err = m_vfs_write_timed(NULL,
                                                    writer_fd,
                                                    payload + offset,
                                                    chunk_size,
                                                    &written,
                                                    &write_deadline);
        if (write_err == M_VFS_ERR_TIMEOUT && written == 0) {
            break;
        }
        DEVFS_TEST_ASSERT(write_err == M_VFS_ERR_OK && written > 0,
                          cleanup,
                          "shm drop: write err=%d written=%u chunk=%u",
                          write_err,
                          (unsigned)written,
                          (unsigned)chunk_size);
        offset += written;
        total_written += written;
    }
    DEVFS_TEST_ASSERT(total_written > 0,
                      cleanup,
                      "shm drop: wrote nothing");

    vTaskDelay(pdMS_TO_TICKS(1));

    uint8_t result[512] = {0};
    size_t read = 0;
    m_timer_deadline_t read_deadline = m_timer_deadline_from_relative(100000);
    m_vfs_error_t read_err =
            m_vfs_read_timed(NULL, reader_fd, result, info.capacity, &read, &read_deadline);
    size_t expected_read = (total_written >= info.capacity) ? info.capacity : total_written;
    DEVFS_TEST_ASSERT(read_err == M_VFS_ERR_OK && read == expected_read,
                      cleanup,
                      "shm drop: read err=%d read=%u expected=%u",
                      read_err,
                      (unsigned)read,
                      (unsigned)expected_read);

    size_t start = (total_written >= expected_read) ? (total_written - expected_read) : 0;
    for (size_t i = 0; i < expected_read; ++i) {
        DEVFS_TEST_ASSERT(result[i] == payload[start + i],
                          cleanup,
                          "shm drop: mismatch idx=%u got=%u expected=%u",
                          (unsigned)i,
                          result[i],
                          payload[start + i]);
    }

cleanup:
    if (reader_fd >= 0) {
        m_vfs_close(NULL, reader_fd);
    }
    if (writer_fd >= 0) {
        m_vfs_close(NULL, writer_fd);
    }
    return ok;
}

static bool
run_test_shm_stream_close_wakes_reader(void)
{
    if (!devfs_tests_prepare_env("shm stream close wake")) {
        return false;
    }

    bool ok = true;
    int reader_fd = -1;
    DEVFS_TEST_ASSERT(m_vfs_open(NULL, "/dev/stream0", 0, &reader_fd) == M_VFS_ERR_OK,
                      cleanup,
                      "shm stream close: open reader failed");

    SemaphoreHandle_t done = xSemaphoreCreateBinary();
    DEVFS_TEST_ASSERT(done != NULL, cleanup, "shm stream close: semaphore alloc failed");

    devfs_shm_waiter_ctx_t ctx = {
        .fd = reader_fd,
        .done = done,
        .result = M_VFS_ERR_OK,
    };

    if (xTaskCreate(devfs_shm_reader_wait_task,
                    "devfs_shm_stream_wait_reader",
                    2048,
                    &ctx,
                    tskIDLE_PRIORITY + 1,
                    NULL) != pdPASS) {
        ESP_LOGE(TAG, "shm stream close: reader task create failed");
        vSemaphoreDelete(done);
        ok = false;
        goto cleanup;
    }

    vTaskDelay(pdMS_TO_TICKS(10));
    m_vfs_close(NULL, reader_fd);
    reader_fd = -1;

    DEVFS_TEST_ASSERT(xSemaphoreTake(done, pdMS_TO_TICKS(1000)) == pdTRUE,
                      cleanup,
                      "shm stream close: reader completion timeout");
    vSemaphoreDelete(done);
    DEVFS_TEST_ASSERT(ctx.result == M_VFS_ERR_DESTROYED,
                      cleanup,
                      "shm stream close: reader err=%d expected=%d",
                      ctx.result,
                      M_VFS_ERR_DESTROYED);

cleanup:
    if (reader_fd >= 0) {
        m_vfs_close(NULL, reader_fd);
    }
    return ok;
}

static bool
run_test_shm_poll_notify(void)
{
    if (!devfs_tests_prepare_env("shm poll")) {
        return false;
    }

    bool ok = true;
    int reader_fd = -1;
    int writer_fd = -1;
    DEVFS_TEST_ASSERT(m_vfs_open(NULL, "/dev/pipe0", 0, &reader_fd) == M_VFS_ERR_OK,
                      cleanup,
                      "shm poll: open reader failed");
    DEVFS_TEST_ASSERT(m_vfs_open(NULL, "/dev/pipe0", 0, &writer_fd) == M_VFS_ERR_OK,
                      cleanup,
                      "shm poll: open writer failed");

    SemaphoreHandle_t done = xSemaphoreCreateBinary();
    DEVFS_TEST_ASSERT(done != NULL, cleanup, "shm poll: semaphore alloc failed");

    const uint8_t payload[] = "poll";
    devfs_shm_writer_ctx_t ctx = {
        .fd = writer_fd,
        .payload = payload,
        .length = sizeof(payload),
        .delay = pdMS_TO_TICKS(10),
        .done = done,
        .success = false,
    };

    if (xTaskCreate(devfs_shm_writer_task,
                    "devfs_shm_poll_writer",
                    2048,
                    &ctx,
                    tskIDLE_PRIORITY + 1,
                    NULL) != pdPASS) {
        ESP_LOGE(TAG, "shm poll: writer task create failed");
        vSemaphoreDelete(done);
        ok = false;
        goto cleanup;
    }

    m_vfs_pollfd_t poll_fd = {
        .fd = reader_fd,
        .events = M_VFS_POLLIN,
    };
    size_t ready = 0;
    m_timer_deadline_t deadline = m_timer_deadline_from_relative(100000);
    m_vfs_error_t poll_err = m_vfs_poll(NULL,
                                        &poll_fd,
                                        1,
                                        &deadline,
                                        &ready);
    DEVFS_TEST_ASSERT(poll_err == M_VFS_ERR_OK && ready == 1 &&
                      (poll_fd.revents & M_VFS_POLLIN) != 0,
                      cleanup,
                      "shm poll: poll err=%d ready=%u revents=0x%x",
                      poll_err,
                      (unsigned)ready,
                      poll_fd.revents);

    size_t read = 0;
    uint8_t sink[16] = {0};
    m_vfs_error_t read_err = m_vfs_read(NULL, reader_fd, sink, sizeof(payload), &read);
    DEVFS_TEST_ASSERT(read_err == M_VFS_ERR_OK && read == sizeof(payload),
                      cleanup,
                      "shm poll: read err=%d read=%u",
                      read_err,
                      (unsigned)read);
    DEVFS_TEST_ASSERT(xSemaphoreTake(done, pdMS_TO_TICKS(1000)) == pdTRUE,
                      cleanup,
                      "shm poll: writer completion timeout");
    vSemaphoreDelete(done);
    DEVFS_TEST_ASSERT(ctx.success, cleanup, "shm poll: writer failed");

cleanup:
    if (reader_fd >= 0) {
        m_vfs_close(NULL, reader_fd);
    }
    if (writer_fd >= 0) {
        m_vfs_close(NULL, writer_fd);
    }
    return ok;
}

#if CONFIG_MAGNOLIA_VFS_STRESS_TESTS

#if CONFIG_MAGNOLIA_DEVFS_PIPES
static bool
run_stress_pipe_close_race(void)
{
    if (!devfs_tests_prepare_env("pipe close race")) {
        return false;
    }

    bool ok = true;
    for (size_t i = 0; i < 20; ++i) {
        int fd = -1;
        DEVFS_TEST_ASSERT(m_vfs_open(NULL, "/dev/pipe0", 0, &fd) == M_VFS_ERR_OK,
                          cleanup_iter,
                          "pipe close race: open failed iter=%u",
                          (unsigned)i);

        SemaphoreHandle_t done = xSemaphoreCreateBinary();
        DEVFS_TEST_ASSERT(done != NULL,
                          cleanup_iter,
                          "pipe close race: sem alloc failed iter=%u",
                          (unsigned)i);

        devfs_pipe_waiter_ctx_t ctx = {
            .fd = fd,
            .done = done,
            .result = M_VFS_ERR_OK,
        };

        if (xTaskCreate(devfs_pipe_reader_wait_task,
                        "devfs_pipe_race_reader",
                        2048,
                        &ctx,
                        tskIDLE_PRIORITY + 1,
                        NULL) != pdPASS) {
            vSemaphoreDelete(done);
            ok = false;
            goto cleanup_iter;
        }

        vTaskDelay(pdMS_TO_TICKS(1));
        m_vfs_close(NULL, fd);
        fd = -1;

        DEVFS_TEST_ASSERT(xSemaphoreTake(done, pdMS_TO_TICKS(1000)) == pdTRUE,
                          cleanup_iter,
                          "pipe close race: reader timeout iter=%u",
                          (unsigned)i);
        vSemaphoreDelete(done);
        DEVFS_TEST_ASSERT(ctx.result == M_VFS_ERR_DESTROYED,
                          cleanup_iter,
                          "pipe close race: reader err=%d iter=%u",
                          ctx.result,
                          (unsigned)i);
        continue;

cleanup_iter:
        if (fd >= 0) {
            m_vfs_close(NULL, fd);
        }
        if (done != NULL) {
            vSemaphoreDelete(done);
        }
        break;
    }

    return ok;
}
#endif /* CONFIG_MAGNOLIA_DEVFS_PIPES */

#if CONFIG_MAGNOLIA_IPC_ENABLED
static bool
run_stress_shm_stream_close_race(void)
{
    if (!devfs_tests_prepare_env("shm stream close race")) {
        return false;
    }

    bool ok = true;
    for (size_t i = 0; i < 20; ++i) {
        int fd = -1;
        DEVFS_TEST_ASSERT(m_vfs_open(NULL, "/dev/stream0", 0, &fd) == M_VFS_ERR_OK,
                          cleanup_iter,
                          "shm stream close race: open failed iter=%u",
                          (unsigned)i);

        SemaphoreHandle_t done = xSemaphoreCreateBinary();
        DEVFS_TEST_ASSERT(done != NULL,
                          cleanup_iter,
                          "shm stream close race: sem alloc failed iter=%u",
                          (unsigned)i);

        devfs_shm_waiter_ctx_t ctx = {
            .fd = fd,
            .done = done,
            .result = M_VFS_ERR_OK,
        };

        if (xTaskCreate(devfs_shm_reader_wait_task,
                        "devfs_shm_stream_race_reader",
                        2048,
                        &ctx,
                        tskIDLE_PRIORITY + 1,
                        NULL) != pdPASS) {
            vSemaphoreDelete(done);
            ok = false;
            goto cleanup_iter;
        }

        vTaskDelay(pdMS_TO_TICKS(1));
        m_vfs_close(NULL, fd);
        fd = -1;

        DEVFS_TEST_ASSERT(xSemaphoreTake(done, pdMS_TO_TICKS(1000)) == pdTRUE,
                          cleanup_iter,
                          "shm stream close race: reader timeout iter=%u",
                          (unsigned)i);
        vSemaphoreDelete(done);
        DEVFS_TEST_ASSERT(ctx.result == M_VFS_ERR_DESTROYED,
                          cleanup_iter,
                          "shm stream close race: reader err=%d iter=%u",
                          ctx.result,
                          (unsigned)i);
        continue;

cleanup_iter:
        if (fd >= 0) {
            m_vfs_close(NULL, fd);
        }
        if (done != NULL) {
            vSemaphoreDelete(done);
        }
        break;
    }

    return ok;
}
#endif /* CONFIG_MAGNOLIA_IPC_ENABLED */

static bool
run_stress_error_injection_smoke(void)
{
    if (!devfs_tests_prepare_env("vfs inject smoke")) {
        return false;
    }

    bool ok = true;
    int fd = -1;

    m_vfs_test_set_error_injection(true, M_VFS_ERR_NO_MEMORY);
    m_vfs_error_t err = m_vfs_open(NULL, "/dev/pipe0", 0, &fd);
    ok &= (err == M_VFS_ERR_NO_MEMORY || err == M_VFS_ERR_BUSY);
    m_vfs_test_set_error_injection(false, M_VFS_ERR_BUSY);

    if (fd >= 0) {
        m_vfs_close(NULL, fd);
    }

    return ok;
}

#endif /* CONFIG_MAGNOLIA_VFS_STRESS_TESTS */

#endif /* CONFIG_MAGNOLIA_IPC_ENABLED */

void
devfs_selftests_run(void)
{
    bool overall = true;
    overall &= test_report("devfs device io", run_test_device_io());
    overall &= test_report("devfs poll", run_test_poll());
    overall &= test_report("devfs unregister wait", run_test_devfs_unregister_wait());
    overall &= test_report("devfs namespace hierarchy",
                           run_test_devfs_namespace());
    overall &= test_report("devfs extended ops",
                           run_test_devfs_extended_ops());
    overall &= test_report("devfs fallback ops",
                           run_test_devfs_fallback_ops());
    overall &= test_report("devfs diagnostics",
                           run_test_devfs_diag_output());
#if CONFIG_MAGNOLIA_DEVFS_PIPES
    overall &= test_report("devfs pipe basic",
                           run_test_devfs_pipe_basic());
    overall &= test_report("devfs pipe close wakes reader",
                           run_test_pipe_close_wakes_reader());
    overall &= test_report("devfs pipe poll close wakes waiter",
                           run_test_pipe_poll_close_wakes_waiter());
    overall &= test_report("devfs pipe close wakes blocked writer",
                           run_test_pipe_close_wakes_blocked_writer());
#endif
#if CONFIG_MAGNOLIA_DEVFS_TTY
    overall &= test_report("devfs tty canonical",
                           run_test_devfs_tty_canonical());
#endif
#if CONFIG_MAGNOLIA_DEVFS_PTY
    overall &= test_report("devfs pty roundtrip",
                           run_test_devfs_pty_basic());
#endif
#if CONFIG_MAGNOLIA_IPC_ENABLED
    ESP_LOGI(TAG, "Starting devfs shm pipe concurrent");
    overall &= test_report("devfs shm pipe concurrent",
                           run_test_shm_pipe_concurrent());
    overall &= test_report("devfs shm pipe close wakes reader",
                           run_test_shm_pipe_close_wakes_reader());
    ESP_LOGI(TAG, "Starting devfs shm pipe timeout");
    overall &= test_report("devfs shm pipe timeout",
                           run_test_shm_pipe_timeout());
    ESP_LOGI(TAG, "Starting devfs shm stream drop");
    overall &= test_report("devfs shm stream drop",
                           run_test_shm_stream_drop());
    overall &= test_report("devfs shm stream close wakes reader",
                           run_test_shm_stream_close_wakes_reader());
    ESP_LOGI(TAG, "Starting devfs shm poll notify");
    overall &= test_report("devfs shm poll notify",
                           run_test_shm_poll_notify());
#endif

#if CONFIG_MAGNOLIA_VFS_STRESS_TESTS
#if CONFIG_MAGNOLIA_DEVFS_PIPES
    overall &= test_report("stress pipe close race",
                           run_stress_pipe_close_race());
#endif
#if CONFIG_MAGNOLIA_IPC_ENABLED
    overall &= test_report("stress shm stream close race",
                           run_stress_shm_stream_close_race());
#endif
    overall &= test_report("stress vfs error injection smoke",
                           run_stress_error_injection_smoke());
#endif
    ESP_LOGI(TAG, "devfs self-tests %s", overall ? "PASSED" : "FAILED");
    devfs_tests_cleanup_env();
}

#else

void
devfs_selftests_run(void)
{
}

#endif /* CONFIG_MAGNOLIA_VFS_DEVFS && CONFIG_MAGNOLIA_DEVFS_SELFTESTS */
