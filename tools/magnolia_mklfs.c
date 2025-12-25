#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "lfs.h"

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#endif

typedef struct {
    int fd;
    uint32_t image_size;
    uint32_t block_size;
} image_ctx_t;

static int
flash_read(const struct lfs_config *cfg,
           lfs_block_t block,
           lfs_off_t off,
           void *buffer,
           lfs_size_t size)
{
    const image_ctx_t *ctx = (const image_ctx_t *)cfg->context;
    if (ctx == NULL || ctx->fd < 0) {
        return LFS_ERR_INVAL;
    }
    uint64_t base = (uint64_t)block * (uint64_t)cfg->block_size;
    uint64_t pos = base + (uint64_t)off;
    if (pos + (uint64_t)size > (uint64_t)ctx->image_size) {
        return LFS_ERR_INVAL;
    }

    uint8_t *out = (uint8_t *)buffer;
    size_t remaining = (size_t)size;
    while (remaining > 0) {
        ssize_t n = pread(ctx->fd, out, remaining, (off_t)pos);
        if (n < 0) {
            return LFS_ERR_IO;
        }
        if (n == 0) {
            return LFS_ERR_IO;
        }
        out += (size_t)n;
        remaining -= (size_t)n;
        pos += (uint64_t)n;
    }
    return 0;
}

static int
flash_prog(const struct lfs_config *cfg,
           lfs_block_t block,
           lfs_off_t off,
           const void *buffer,
           lfs_size_t size)
{
    const image_ctx_t *ctx = (const image_ctx_t *)cfg->context;
    if (ctx == NULL || ctx->fd < 0) {
        return LFS_ERR_INVAL;
    }
    uint64_t base = (uint64_t)block * (uint64_t)cfg->block_size;
    uint64_t pos = base + (uint64_t)off;
    if (pos + (uint64_t)size > (uint64_t)ctx->image_size) {
        return LFS_ERR_INVAL;
    }

    uint8_t *tmp = (uint8_t *)malloc((size_t)size);
    if (tmp == NULL) {
        return LFS_ERR_NOMEM;
    }

    if (flash_read(cfg, block, off, tmp, size) < 0) {
        free(tmp);
        return LFS_ERR_IO;
    }

    const uint8_t *in = (const uint8_t *)buffer;
    for (size_t i = 0; i < (size_t)size; ++i) {
        tmp[i] = (uint8_t)(tmp[i] & in[i]);
    }

    const uint8_t *out = tmp;
    size_t remaining = (size_t)size;
    while (remaining > 0) {
        ssize_t n = pwrite(ctx->fd, out, remaining, (off_t)pos);
        if (n < 0) {
            free(tmp);
            return LFS_ERR_IO;
        }
        if (n == 0) {
            free(tmp);
            return LFS_ERR_IO;
        }
        out += (size_t)n;
        remaining -= (size_t)n;
        pos += (uint64_t)n;
    }

    free(tmp);
    return 0;
}

static int
flash_erase(const struct lfs_config *cfg, lfs_block_t block)
{
    const image_ctx_t *ctx = (const image_ctx_t *)cfg->context;
    if (ctx == NULL || ctx->fd < 0) {
        return LFS_ERR_INVAL;
    }
    uint64_t pos = (uint64_t)block * (uint64_t)cfg->block_size;
    if (pos + (uint64_t)cfg->block_size > (uint64_t)ctx->image_size) {
        return LFS_ERR_INVAL;
    }

    uint8_t buf[4096];
    memset(buf, 0xff, sizeof(buf));
    size_t remaining = (size_t)cfg->block_size;
    while (remaining > 0) {
        size_t chunk = remaining;
        if (chunk > sizeof(buf)) {
            chunk = sizeof(buf);
        }
        ssize_t n = pwrite(ctx->fd, buf, chunk, (off_t)pos);
        if (n < 0) {
            return LFS_ERR_IO;
        }
        if (n == 0) {
            return LFS_ERR_IO;
        }
        remaining -= (size_t)n;
        pos += (uint64_t)n;
    }
    return 0;
}

static int
flash_sync(const struct lfs_config *cfg)
{
    const image_ctx_t *ctx = (const image_ctx_t *)cfg->context;
    if (ctx == NULL || ctx->fd < 0) {
        return LFS_ERR_INVAL;
    }
    return (fsync(ctx->fd) == 0) ? 0 : LFS_ERR_IO;
}

static bool
join_path(const char *parent, const char *name, char *out, size_t out_cap)
{
    if (name == NULL || out == NULL || out_cap == 0) {
        return false;
    }
    if (parent == NULL || parent[0] == '\0') {
        int n = snprintf(out, out_cap, "%s", name);
        return (n >= 0 && (size_t)n < out_cap);
    }
    int n = snprintf(out, out_cap, "%s/%s", parent, name);
    return (n >= 0 && (size_t)n < out_cap);
}

static int
copy_file_to_lfs(lfs_t *lfs, const char *host_path, const char *lfs_path)
{
    int fd = open(host_path, O_RDONLY);
    if (fd < 0) {
        fprintf(stderr, "open(%s) failed: %s\n", host_path, strerror(errno));
        return -1;
    }

    lfs_file_t out;
    int err = lfs_file_open(lfs, &out, lfs_path, LFS_O_CREAT | LFS_O_WRONLY | LFS_O_TRUNC);
    if (err < 0) {
        fprintf(stderr, "lfs_file_open(%s) failed: %d\n", lfs_path, err);
        close(fd);
        return err;
    }

    uint8_t buf[1024];
    while (true) {
        ssize_t n = read(fd, buf, sizeof(buf));
        if (n < 0) {
            fprintf(stderr, "read(%s) failed: %s\n", host_path, strerror(errno));
            err = -1;
            break;
        }
        if (n == 0) {
            break;
        }
        lfs_ssize_t w = lfs_file_write(lfs, &out, buf, (lfs_size_t)n);
        if (w < 0 || w != n) {
            fprintf(stderr, "lfs_file_write(%s) failed: %ld\n", lfs_path, (long)w);
            err = (int)w;
            break;
        }
    }
#
    (void)lfs_file_close(lfs, &out);
    close(fd);
    return err < 0 ? err : 0;
}

static int
copy_tree_to_lfs(lfs_t *lfs, const char *host_dir, const char *lfs_dir)
{
    DIR *dir = opendir(host_dir);
    if (dir == NULL) {
        fprintf(stderr, "opendir(%s) failed: %s\n", host_dir, strerror(errno));
        return -1;
    }

    struct dirent *de = NULL;
    while ((de = readdir(dir)) != NULL) {
        if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0) {
            continue;
        }

        char host_path[1024];
        char lfs_path[1024];
        if (!join_path(host_dir, de->d_name, host_path, sizeof(host_path)) ||
            !join_path(lfs_dir, de->d_name, lfs_path, sizeof(lfs_path))) {
            closedir(dir);
            fprintf(stderr, "path too long: %s/%s\n", host_dir, de->d_name);
            return -1;
        }

        struct stat st;
        if (lstat(host_path, &st) != 0) {
            closedir(dir);
            fprintf(stderr, "stat(%s) failed: %s\n", host_path, strerror(errno));
            return -1;
        }

        if (S_ISDIR(st.st_mode)) {
            int err = lfs_mkdir(lfs, lfs_path);
            if (err < 0 && err != LFS_ERR_EXIST) {
                closedir(dir);
                fprintf(stderr, "lfs_mkdir(%s) failed: %d\n", lfs_path, err);
                return err;
            }
            err = copy_tree_to_lfs(lfs, host_path, lfs_path);
            if (err < 0) {
                closedir(dir);
                return err;
            }
        } else if (S_ISREG(st.st_mode)) {
            int err = copy_file_to_lfs(lfs, host_path, lfs_path);
            if (err < 0) {
                closedir(dir);
                return err;
            }
        } else {
            /* Skip symlinks/dev nodes. */
        }
    }

    closedir(dir);
    return 0;
}

static bool
parse_u32(const char *s, uint32_t *out)
{
    if (s == NULL || out == NULL) {
        return false;
    }
    errno = 0;
    char *end = NULL;
    unsigned long v = strtoul(s, &end, 0);
    if (errno != 0 || end == s || *end != '\0') {
        return false;
    }
    if (v > UINT32_MAX) {
        return false;
    }
    *out = (uint32_t)v;
    return true;
}

static bool
parse_i32(const char *s, int32_t *out)
{
    if (s == NULL || out == NULL) {
        return false;
    }
    errno = 0;
    char *end = NULL;
    long v = strtol(s, &end, 0);
    if (errno != 0 || end == s || *end != '\0') {
        return false;
    }
    if (v < INT32_MIN || v > INT32_MAX) {
        return false;
    }
    *out = (int32_t)v;
    return true;
}

static void
usage(const char *argv0)
{
    fprintf(stderr,
            "Usage: %s <source_dir> <output_image> <image_size> [block_size read_size prog_size cache_size lookahead_size block_cycles]\n",
            argv0);
}

int
main(int argc, char **argv)
{
    if (argc < 4) {
        usage(argv[0]);
        return 2;
    }

    const char *source_dir = argv[1];
    const char *output_image = argv[2];

    uint32_t image_size = 0;
    if (!parse_u32(argv[3], &image_size) || image_size == 0) {
        fprintf(stderr, "invalid image_size: %s\n", argv[3]);
        return 2;
    }

    uint32_t block_size = 4096;
    uint32_t read_size = 128;
    uint32_t prog_size = 128;
    uint32_t cache_size = 512;
    uint32_t lookahead_size = 64;
    int32_t block_cycles = 128;

    if (argc > 4 && !parse_u32(argv[4], &block_size)) {
        fprintf(stderr, "invalid block_size: %s\n", argv[4]);
        return 2;
    }
    if (argc > 5 && !parse_u32(argv[5], &read_size)) {
        fprintf(stderr, "invalid read_size: %s\n", argv[5]);
        return 2;
    }
    if (argc > 6 && !parse_u32(argv[6], &prog_size)) {
        fprintf(stderr, "invalid prog_size: %s\n", argv[6]);
        return 2;
    }
    if (argc > 7 && !parse_u32(argv[7], &cache_size)) {
        fprintf(stderr, "invalid cache_size: %s\n", argv[7]);
        return 2;
    }
    if (argc > 8 && !parse_u32(argv[8], &lookahead_size)) {
        fprintf(stderr, "invalid lookahead_size: %s\n", argv[8]);
        return 2;
    }
    if (argc > 9 && !parse_i32(argv[9], &block_cycles)) {
        fprintf(stderr, "invalid block_cycles: %s\n", argv[9]);
        return 2;
    }

    if (block_size == 0 || (image_size % block_size) != 0) {
        fprintf(stderr, "image_size must be multiple of block_size (size=%u block=%u)\n",
                (unsigned)image_size, (unsigned)block_size);
        return 2;
    }

    int fd = open(output_image, O_CREAT | O_TRUNC | O_RDWR, 0644);
    if (fd < 0) {
        fprintf(stderr, "open(%s) failed: %s\n", output_image, strerror(errno));
        return 1;
    }

    uint8_t ff[4096];
    memset(ff, 0xff, sizeof(ff));
    uint32_t remaining = image_size;
    while (remaining > 0) {
        size_t chunk = remaining;
        if (chunk > sizeof(ff)) {
            chunk = sizeof(ff);
        }
        ssize_t n = write(fd, ff, chunk);
        if (n < 0) {
            fprintf(stderr, "write(%s) failed: %s\n", output_image, strerror(errno));
            close(fd);
            return 1;
        }
        remaining -= (uint32_t)n;
    }

    image_ctx_t ctx = {
        .fd = fd,
        .image_size = image_size,
        .block_size = block_size,
    };

    struct lfs_config cfg = {0};
    cfg.context = &ctx;
    cfg.read = flash_read;
    cfg.prog = flash_prog;
    cfg.erase = flash_erase;
    cfg.sync = flash_sync;
    cfg.read_size = read_size;
    cfg.prog_size = prog_size;
    cfg.block_size = block_size;
    cfg.block_count = image_size / block_size;
    cfg.block_cycles = block_cycles;
    cfg.cache_size = cache_size;
    cfg.lookahead_size = lookahead_size;
    cfg.compact_thresh = 0;
    cfg.read_buffer = NULL;
    cfg.prog_buffer = NULL;
    cfg.lookahead_buffer = NULL;

    lfs_t lfs;
    memset(&lfs, 0, sizeof(lfs));

    int err = lfs_format(&lfs, &cfg);
    if (err < 0) {
        fprintf(stderr, "lfs_format failed: %d\n", err);
        close(fd);
        return 1;
    }

    err = lfs_mount(&lfs, &cfg);
    if (err < 0) {
        fprintf(stderr, "lfs_mount failed: %d\n", err);
        close(fd);
        return 1;
    }

    err = copy_tree_to_lfs(&lfs, source_dir, "");
    if (err < 0) {
        fprintf(stderr, "copy_tree_to_lfs failed\n");
        (void)lfs_unmount(&lfs);
        close(fd);
        return 1;
    }

    err = lfs_unmount(&lfs);
    if (err < 0) {
        fprintf(stderr, "lfs_unmount failed: %d\n", err);
        close(fd);
        return 1;
    }

    close(fd);
    return 0;
}
