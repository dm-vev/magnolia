#include "sdkconfig.h"

#if CONFIG_MAGNOLIA_LITTLEFS_ENABLED && CONFIG_MAGNOLIA_VFS_LITTLEFS_SELFTESTS

#include <fcntl.h>
#include <inttypes.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "esp_err.h"
#include "esp_log.h"
#include "esp_partition.h"
#include "esp_rom_sys.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "kernel/core/vfs/m_vfs.h"
#include "kernel/core/vfs/core/m_vfs_object.h"
#include "kernel/core/elf/m_elf_loader.h"
#include "kernel/vfs/fs/littlefs/littlefs_fs.h"

#ifndef CONFIG_MAGNOLIA_LITTLEFS_TEST_WEAR_CYCLES
#define CONFIG_MAGNOLIA_LITTLEFS_TEST_WEAR_CYCLES 200
#endif

#ifndef CONFIG_MAGNOLIA_LITTLEFS_TEST_DESTRUCTIVE
#define CONFIG_MAGNOLIA_LITTLEFS_TEST_DESTRUCTIVE 1
#endif

static const char *TAG = "littlefs_tests";

#define LFS_TEST_PREFIX "[LFS-TEST]"

static void
log_step(const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    char buf[256];
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    ESP_LOGI(TAG, "%s %s", LFS_TEST_PREFIX, buf);
}

static void
log_error(const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    char buf[256];
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    ESP_LOGE(TAG, "%s %s", LFS_TEST_PREFIX, buf);
}

static bool
check_step(const char *step, m_vfs_error_t err, m_vfs_error_t expected)
{
    if (err == expected) {
        log_step("%s -> OK", step);
        return true;
    }
    log_error("%s -> err=%d expected=%d", step, err, expected);
    return false;
}

static const esp_partition_t *
find_lfs_partition(const char *label)
{
    const esp_partition_t *p = esp_partition_find_first(
            ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, label);
    if (p == NULL) {
        log_error("partition '%s' not found", label);
        return NULL;
    }
    log_step("partition found: label=%s type=0x%02x subtype=0x%02x",
             p->label, p->type, p->subtype);
    log_step("partition addr=0x%08"PRIx32" size=%"PRIu32" erase=%"PRIu32,
             p->address, p->size, p->erase_size);
    return p;
}

static bool
phase1_flash_api(const esp_partition_t *p)
{
#if !CONFIG_MAGNOLIA_LITTLEFS_TEST_DESTRUCTIVE
    (void)p;
    log_step("phase1 flash api: SKIP (destructive disabled)");
    return true;
#else
    esp_rom_printf("%s phase1 flash api start\n", LFS_TEST_PREFIX);
    uint8_t *sector = pvPortMalloc(4096);
    uint8_t *pattern = pvPortMalloc(256);
    uint8_t *verify = pvPortMalloc(256);
    if (sector == NULL || pattern == NULL || verify == NULL) {
        if (sector) vPortFree(sector);
        if (pattern) vPortFree(pattern);
        if (verify) vPortFree(verify);
        esp_rom_printf("%s phase1 flash api: OOM\n", LFS_TEST_PREFIX);
        return false;
    }
    memset(pattern, 0xA5, 256);

    esp_err_t err = esp_partition_read(p, 0, sector, 4096);
    if (err != ESP_OK) {
        esp_rom_printf("%s flash_read addr=0x%08"PRIx32" len=%u err=%d\n",
                       LFS_TEST_PREFIX, p->address, 4096u, (int)err);
        vPortFree(sector);
        vPortFree(pattern);
        vPortFree(verify);
        return false;
    }
    esp_rom_printf("%s flash_read addr=0x%08"PRIx32" len=%u OK\n",
                   LFS_TEST_PREFIX, p->address, 4096u);

    err = esp_partition_erase_range(p, 0, p->erase_size);
    if (err != ESP_OK) {
        esp_rom_printf("%s flash_erase addr=0x%08"PRIx32" len=%"PRIu32" err=%d\n",
                       LFS_TEST_PREFIX, p->address, p->erase_size, (int)err);
        vPortFree(sector);
        vPortFree(pattern);
        vPortFree(verify);
        return false;
    }
    esp_rom_printf("%s flash_erase addr=0x%08"PRIx32" len=%"PRIu32" OK\n",
                   LFS_TEST_PREFIX, p->address, p->erase_size);

    err = esp_partition_write(p, 0, pattern, 256);
    if (err != ESP_OK) {
        esp_rom_printf("%s flash_write addr=0x%08"PRIx32" len=%u err=%d\n",
                       LFS_TEST_PREFIX, p->address, 256u, (int)err);
        vPortFree(sector);
        vPortFree(pattern);
        vPortFree(verify);
        return false;
    }
    esp_rom_printf("%s flash_write addr=0x%08"PRIx32" len=%u OK\n",
                   LFS_TEST_PREFIX, p->address, 256u);

    err = esp_partition_read(p, 0, verify, 256);
    if (err != ESP_OK || memcmp(verify, pattern, 256) != 0) {
        esp_rom_printf("%s flash_verify failed err=%d cmp=%d\n",
                       LFS_TEST_PREFIX, (int)err, memcmp(verify, pattern, 256));
        vPortFree(sector);
        vPortFree(pattern);
        vPortFree(verify);
        return false;
    }
    esp_rom_printf("%s flash_verify OK\n", LFS_TEST_PREFIX);

    err = esp_partition_erase_range(p, 0, p->erase_size);
    if (err != ESP_OK) {
        esp_rom_printf("%s flash_cleanup erase err=%d\n", LFS_TEST_PREFIX, (int)err);
        vPortFree(sector);
        vPortFree(pattern);
        vPortFree(verify);
        return false;
    }
    memset(verify, 0, 256);
    err = esp_partition_read(p, 0, verify, 256);
    bool cleared = true;
    for (size_t i = 0; i < 256; ++i) {
        if (verify[i] != 0xFF) {
            cleared = false;
            break;
        }
    }
    esp_rom_printf("%s flash_cleanup -> %s\n", LFS_TEST_PREFIX,
                   cleared ? "OK" : "NOT_CLEARED");
    vPortFree(sector);
    vPortFree(pattern);
    vPortFree(verify);
    return cleared;
#endif
}

static bool
create_file_vfs(const char *path)
{
    if (path == NULL) {
        return false;
    }

    const char *last_slash = strrchr(path, '/');
    if (last_slash == NULL || last_slash == path) {
        log_error("invalid path for create: %s", path);
        return false;
    }

    char parent_path[M_VFS_PATH_MAX_LEN];
    size_t parent_len = (size_t)(last_slash - path);
    if (parent_len >= sizeof(parent_path)) {
        log_error("parent path too long: %s", path);
        return false;
    }
    memcpy(parent_path, path, parent_len);
    parent_path[parent_len] = '\0';

    const char *leaf = last_slash + 1;
    if (leaf[0] == '\0') {
        log_error("empty leaf for create: %s", path);
        return false;
    }

    m_vfs_path_t parsed_parent;
    if (!m_vfs_path_parse(parent_path, &parsed_parent)) {
        log_error("parse parent %s failed", parent_path);
        return false;
    }

    m_vfs_node_t *parent_node = NULL;
    if (m_vfs_path_resolve(NULL, &parsed_parent, &parent_node) != M_VFS_ERR_OK ||
        parent_node == NULL) {
        log_error("resolve parent %s failed", parent_path);
        return false;
    }

    bool ok = false;
    if (parent_node->fs_type != NULL &&
        parent_node->fs_type->ops != NULL &&
        parent_node->fs_type->ops->create != NULL) {
        ok = (parent_node->fs_type->ops->create(parent_node->mount,
                                                parent_node,
                                                leaf,
                                                0,
                                                NULL) == M_VFS_ERR_OK);
    }
    m_vfs_node_release(parent_node);
    if (!ok) {
        log_error("create file %s failed", path);
    } else {
        log_step("create file %s -> OK", path);
    }
    return ok;
}

static bool
write_pattern(int fd, uint32_t seed, size_t size)
{
    uint8_t buf[512];
    size_t total = 0;
    while (total < size) {
        size_t chunk = size - total;
        if (chunk > sizeof(buf)) {
            chunk = sizeof(buf);
        }
        for (size_t i = 0; i < chunk; ++i) {
            buf[i] = (uint8_t)((seed + total + i) & 0xFF);
        }
        size_t written = 0;
        m_vfs_error_t err = m_vfs_write(NULL, fd, buf, chunk, &written);
        if (err != M_VFS_ERR_OK || written != chunk) {
            log_error("write chunk off=%u size=%u err=%d written=%u",
                      (unsigned)total, (unsigned)chunk, err, (unsigned)written);
            return false;
        }
        total += chunk;
    }
    return true;
}

static bool
verify_pattern(int fd, uint32_t seed, size_t size)
{
    uint8_t buf[512];
    size_t total = 0;
    while (total < size) {
        size_t chunk = size - total;
        if (chunk > sizeof(buf)) {
            chunk = sizeof(buf);
        }
        size_t read = 0;
        m_vfs_error_t err = m_vfs_read(NULL, fd, buf, chunk, &read);
        if (err != M_VFS_ERR_OK || read != chunk) {
            log_error("read chunk off=%u size=%u err=%d read=%u",
                      (unsigned)total, (unsigned)chunk, err, (unsigned)read);
            return false;
        }
        for (size_t i = 0; i < chunk; ++i) {
            uint8_t expected = (uint8_t)((seed + total + i) & 0xFF);
            if (buf[i] != expected) {
                log_error("verify mismatch off=%u got=%02x exp=%02x",
                          (unsigned)(total + i), buf[i], expected);
                return false;
            }
        }
        total += chunk;
    }
    return true;
}

static bool
phase2_mount_cycles(littlefs_mount_options_t *opts)
{
    log_step("phase2 mount/unmount cycles start");

    for (int i = 0; i < 10; ++i) {
        m_vfs_error_t err = m_vfs_mount("/flash", "littlefs", opts);
        if (err != M_VFS_ERR_OK) {
            log_error("mount cycle %d err=%d", i, err);
            return false;
        }
        log_step("mount cycle %d OK", i);
        err = m_vfs_unmount("/flash");
        if (err != M_VFS_ERR_OK) {
            log_error("unmount cycle %d err=%d", i, err);
            return false;
        }
        log_step("unmount cycle %d OK", i);
    }
    return true;
}

static bool
phase3_basic_files(void)
{
    log_step("phase3 basic files start");

    check_step("mkdir /flash/t", m_vfs_mkdir(NULL, "/flash/t", 0), M_VFS_ERR_OK);

    const struct { const char *path; size_t size; } cases[] = {
        {"/flash/t/small.bin", 32},
        {"/flash/t/block.bin", 4096},
        {"/flash/t/multi.bin", 32768},
    };

    for (size_t i = 0; i < sizeof(cases)/sizeof(cases[0]); ++i) {
        const char *path = cases[i].path;
        size_t size = cases[i].size;
        log_step("create/write/read case path=%s size=%u", path, (unsigned)size);
        if (!create_file_vfs(path)) {
            return false;
        }
        int fd = -1;
        if (!check_step("open file", m_vfs_open(NULL, path, O_RDWR, &fd), M_VFS_ERR_OK)) {
            return false;
        }
        if (!write_pattern(fd, (uint32_t)i * 17u, size)) {
            m_vfs_close(NULL, fd);
            return false;
        }
        m_vfs_close(NULL, fd);
        if (!check_step("reopen ro", m_vfs_open(NULL, path, O_RDONLY, &fd), M_VFS_ERR_OK)) {
            return false;
        }
        bool ok = verify_pattern(fd, (uint32_t)i * 17u, size);
        m_vfs_close(NULL, fd);
        if (!ok) {
            return false;
        }
        check_step("unlink file", m_vfs_unlink(NULL, path), M_VFS_ERR_OK);
    }

    log_step("append test start");
    const char *apath = "/flash/t/app.bin";
    if (!create_file_vfs(apath)) {
        return false;
    }
    int afd = -1;
    if (!check_step("open append", m_vfs_open(NULL, apath, O_RDWR, &afd), M_VFS_ERR_OK)) {
        return false;
    }
    if (!write_pattern(afd, 0x10, 1024)) {
        m_vfs_close(NULL, afd);
        return false;
    }
    m_vfs_close(NULL, afd);
    if (!check_step("reopen append", m_vfs_open(NULL, apath, O_RDWR | O_APPEND, &afd), M_VFS_ERR_OK)) {
        return false;
    }
    if (!write_pattern(afd, 0x20, 512)) {
        m_vfs_close(NULL, afd);
        return false;
    }
    m_vfs_close(NULL, afd);

    if (!check_step("verify append seg1", m_vfs_open(NULL, apath, O_RDONLY, &afd), M_VFS_ERR_OK)) {
        return false;
    }
    bool ok = verify_pattern(afd, 0x10, 1024);
    m_vfs_close(NULL, afd);
    afd = -1;
    if (ok) {
        if (!check_step("verify append seg2", m_vfs_open(NULL, apath, O_RDONLY, &afd), M_VFS_ERR_OK)) {
            return false;
        }
        // skip first segment by reading it
        ok = verify_pattern(afd, 0x10, 1024);
        ok &= verify_pattern(afd, 0x20, 512);
        m_vfs_close(NULL, afd);
        afd = -1;
    }
    if (!ok) {
        return false;
    }
    check_step("unlink append file", m_vfs_unlink(NULL, apath), M_VFS_ERR_OK);

    log_step("truncate test SKIP (no VFS truncate API)");
    return true;
}

static bool
phase4_dirs(void)
{
    log_step("phase4 dirs/readdir start");
    check_step("mkdir /flash/a", m_vfs_mkdir(NULL, "/flash/a", 0), M_VFS_ERR_OK);
    check_step("mkdir /flash/a/b", m_vfs_mkdir(NULL, "/flash/a/b", 0), M_VFS_ERR_OK);
    check_step("mkdir /flash/a/b/c", m_vfs_mkdir(NULL, "/flash/a/b/c", 0), M_VFS_ERR_OK);

    int fd = -1;
    if (!check_step("open /flash/a", m_vfs_open(NULL, "/flash/a", O_RDONLY, &fd), M_VFS_ERR_OK)) {
        return false;
    }
    m_vfs_dirent_t ents[8];
    size_t populated = 0;
    if (!check_step("readdir /a", m_vfs_readdir(NULL, fd, ents, 8, &populated), M_VFS_ERR_OK)) {
        m_vfs_close(NULL, fd);
        return false;
    }
    log_step("readdir /a -> entries=%u", (unsigned)populated);
    m_vfs_close(NULL, fd);
    return true;
}

static bool
phase5_stress(const esp_partition_t *p)
{
    (void)p;
    log_step("phase5 stress start");

    // many small files
    check_step("mkdir /flash/s", m_vfs_mkdir(NULL, "/flash/s", 0), M_VFS_ERR_OK);
    for (int i = 0; i < 200; ++i) {
        char path[64];
        snprintf(path, sizeof(path), "/flash/s/f%03d", i);
        if (!create_file_vfs(path)) {
            return false;
        }
        int fd = -1;
        if (m_vfs_open(NULL, path, O_RDWR, &fd) != M_VFS_ERR_OK) {
            return false;
        }
        uint8_t byte = (uint8_t)i;
        size_t written = 0;
        if (m_vfs_write(NULL, fd, &byte, 1, &written) != M_VFS_ERR_OK) {
            m_vfs_close(NULL, fd);
            return false;
        }
        m_vfs_close(NULL, fd);
    }
    for (int i = 0; i < 200; i += 2) {
        char path[64];
        snprintf(path, sizeof(path), "/flash/s/f%03d", i);
        m_vfs_unlink(NULL, path);
    }
    log_step("stress small files OK");

    log_step("big file stress SKIP (needs statfs)");
    return true;
}

typedef struct {
    int id;
    SemaphoreHandle_t done;
    bool ok;
} lfs_parallel_ctx_t;

static void
parallel_task(void *arg)
{
    lfs_parallel_ctx_t *ctx = arg;
    char dir[32];
    snprintf(dir, sizeof(dir), "/flash/p%d", ctx->id);
    ctx->ok = (m_vfs_mkdir(NULL, dir, 0) == M_VFS_ERR_OK);
    for (int i = 0; ctx->ok && i < 30; ++i) {
        char path[64];
        snprintf(path, sizeof(path), "%s/f%d", dir, i);
        ctx->ok &= create_file_vfs(path);
        int fd = -1;
        ctx->ok &= (m_vfs_open(NULL, path, O_RDWR, &fd) == M_VFS_ERR_OK);
        if (ctx->ok) {
            uint8_t v = (uint8_t)(ctx->id ^ i);
            size_t w = 0;
            ctx->ok &= (m_vfs_write(NULL, fd, &v, 1, &w) == M_VFS_ERR_OK);
        }
        if (fd >= 0) {
            m_vfs_close(NULL, fd);
        }
    }
    log_step("job=%d parallel write %s", ctx->id, ctx->ok ? "OK" : "FAIL");
    xSemaphoreGive(ctx->done);
    vTaskDelete(NULL);
}

static bool
phase6_parallel(void)
{
    log_step("phase6 parallel start");
    SemaphoreHandle_t done = xSemaphoreCreateCounting(4, 0);
    if (done == NULL) {
        log_error("no semaphore");
        return false;
    }

    lfs_parallel_ctx_t ctxs[2] = {
        {.id = 1, .done = done, .ok = false},
        {.id = 2, .done = done, .ok = false},
    };

    xTaskCreate(parallel_task, "lfs_p1", 4096, &ctxs[0], 5, NULL);
    xTaskCreate(parallel_task, "lfs_p2", 4096, &ctxs[1], 5, NULL);

    bool ok = true;
    for (int i = 0; i < 2; ++i) {
        ok &= (xSemaphoreTake(done, pdMS_TO_TICKS(10000)) == pdTRUE);
    }
    vSemaphoreDelete(done);
    ok &= ctxs[0].ok && ctxs[1].ok;
    return ok;
}

static bool
phase7_powerloss(void)
{
#if !CONFIG_MAGNOLIA_LITTLEFS_TEST_POWERLOSS
    log_step("phase7 powerloss SKIP (disabled)");
    return true;
#else
    const char *marker = "/flash/powerloss.marker";
    int fd = -1;
    if (m_vfs_open(NULL, marker, O_RDONLY, &fd) == M_VFS_ERR_OK) {
        m_vfs_close(NULL, fd);
        log_step("powerloss post-reboot verification start");
        const char *data = "/flash/powerloss.data";
        if (!check_step("open data", m_vfs_open(NULL, data, O_RDONLY, &fd), M_VFS_ERR_OK)) {
            return false;
        }
        bool ok = verify_pattern(fd, 0x55, 4096);
        m_vfs_close(NULL, fd);
        check_step("unlink marker", m_vfs_unlink(NULL, marker), M_VFS_ERR_OK);
        check_step("unlink data", m_vfs_unlink(NULL, data), M_VFS_ERR_OK);
        log_step("powerloss post-reboot %s", ok ? "OK" : "FAIL");
        return ok;
    }

    log_step("powerloss simulate reboot mid-write");
    create_file_vfs(marker);
    create_file_vfs("/flash/powerloss.data");
    if (!check_step("open data rw",
                    m_vfs_open(NULL, "/flash/powerloss.data", O_RDWR, &fd),
                    M_VFS_ERR_OK)) {
        return false;
    }
    // write half, reboot
    if (!write_pattern(fd, 0x55, 2048)) {
        m_vfs_close(NULL, fd);
        return false;
    }
    log_step("powerloss simulated at mid-write, rebooting");
    m_vfs_close(NULL, fd);
    esp_restart();
    return false;
#endif
}

static bool
phase8_wear(void)
{
    log_step("phase8 wear start cycles=%d", CONFIG_MAGNOLIA_LITTLEFS_TEST_WEAR_CYCLES);
    for (int i = 0; i < CONFIG_MAGNOLIA_LITTLEFS_TEST_WEAR_CYCLES; ++i) {
        char path[48];
        snprintf(path, sizeof(path), "/flash/w%04d", i);
        if (!create_file_vfs(path)) {
            return false;
        }
        int fd = -1;
        if (m_vfs_open(NULL, path, O_RDWR, &fd) != M_VFS_ERR_OK) {
            return false;
        }
        uint8_t v = (uint8_t)i;
        size_t w = 0;
        if (m_vfs_write(NULL, fd, &v, 1, &w) != M_VFS_ERR_OK) {
            m_vfs_close(NULL, fd);
            return false;
        }
        m_vfs_close(NULL, fd);
        if (m_vfs_unlink(NULL, path) != M_VFS_ERR_OK) {
            return false;
        }
        if ((i % 50) == 0) {
            log_step("wear cycle %d OK", i);
        }
    }
    return true;
}

static bool
phase9_injection(void)
{
    log_step("phase9 OOM/flash error injection SKIP (no hooks)");
    return true;
}

void littlefs_selftests_run(void)
{
    const char *label = CONFIG_MAGNOLIA_LITTLEFS_PARTITION_LABEL;
    log_step("selftests start label=%s", label);

    const esp_partition_t *p = find_lfs_partition(label);
    if (p == NULL) {
        log_error("abort: partition missing");
        return;
    }

    bool ok = true;
    ok &= phase1_flash_api(p);

    littlefs_mount_options_t opts = {
        .partition_label = label,
        .format_if_mount_fails = true,
        .read_only = false,
        .format_if_empty = true,
        .force_format = CONFIG_MAGNOLIA_LITTLEFS_TEST_DESTRUCTIVE,
    };

    ok &= check_step("vfs_init", m_vfs_init(), M_VFS_ERR_OK);

    log_step("phase2 mount/format");
    if (opts.force_format) {
        check_step("mount /flash", m_vfs_mount("/flash", "littlefs", &opts), M_VFS_ERR_OK);
    } else {
        check_step("mount /flash", m_vfs_mount("/flash", "littlefs", &opts), M_VFS_ERR_OK);
    }

    ok &= phase3_basic_files();
    ok &= phase4_dirs();
    ok &= phase5_stress(p);
    ok &= phase6_parallel();
    ok &= phase7_powerloss();
    ok &= phase8_wear();
    ok &= phase9_injection();

#if CONFIG_MAGNOLIA_ELF_ENABLED && CONFIG_MAGNOLIA_ELF_APPLETS_SELFTESTS
    {
        int rc = -1;
        ok &= check_step("run /flash/elftest", m_elf_run_file("/flash/elftest", 0, NULL, &rc), 0);
        ok &= (rc == 0);
        log_step("elftest rc=%d", rc);
    }
#endif

    check_step("unmount /flash", m_vfs_unmount("/flash"), M_VFS_ERR_OK);
    ok &= phase2_mount_cycles(&opts);

    ESP_LOGI(TAG, "%s selftests %s", LFS_TEST_PREFIX, ok ? "PASS" : "FAIL");
}

#endif /* CONFIG_MAGNOLIA_LITTLEFS_ENABLED && CONFIG_MAGNOLIA_VFS_LITTLEFS_SELFTESTS */
