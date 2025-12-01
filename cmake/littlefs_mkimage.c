#include <dirent.h>
#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "lfs.h"

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

#define HOST_COPY_BUFFER_SIZE 8192

typedef struct {
    uint8_t *buffer;
    size_t size;
} image_context_t;

static int block_device_read(const struct lfs_config *cfg, lfs_block_t block,
                             lfs_off_t off, void *buffer, lfs_size_t size)
{
    image_context_t *ctx = (image_context_t *)cfg->context;
    size_t index = (size_t)block * cfg->block_size + off;
    memcpy(buffer, ctx->buffer + index, size);
    return 0;
}

static int block_device_prog(const struct lfs_config *cfg, lfs_block_t block,
                             lfs_off_t off, const void *buffer, lfs_size_t size)
{
    image_context_t *ctx = (image_context_t *)cfg->context;
    size_t index = (size_t)block * cfg->block_size + off;
    memcpy(ctx->buffer + index, buffer, size);
    return 0;
}

static int block_device_erase(const struct lfs_config *cfg, lfs_block_t block)
{
    image_context_t *ctx = (image_context_t *)cfg->context;
    size_t index = (size_t)block * cfg->block_size;
    memset(ctx->buffer + index, 0xFF, cfg->block_size);
    return 0;
}

static int block_device_sync(const struct lfs_config *cfg)
{
    (void)cfg;
    return 0;
}

static int join_path(char *dst, size_t size, const char *base, const char *relative)
{
    int needed = 0;

    if (relative && relative[0] != '\0') {
        needed = snprintf(dst, size, "%s/%s", base, relative);
    } else {
        needed = snprintf(dst, size, "%s", base);
    }

    if (needed < 0 || (size_t)needed >= size) {
        return -LFS_ERR_NOMEM;
    }

    return 0;
}

static int build_lfs_path(char *dst, size_t size, const char *relative)
{
    if (relative[0] == '\0') {
        if (size < 2) {
            return -LFS_ERR_NOMEM;
        }
        dst[0] = '/';
        dst[1] = '\0';
        return 0;
    }

    int needed = snprintf(dst, size, "/%s", relative);
    if (needed < 0 || (size_t)needed >= size) {
        return -LFS_ERR_NOMEM;
    }

    return 0;
}

static int copy_file(lfs_t *lfs, const char *host_path, const char *relative, bool verbose)
{
    FILE *fp = fopen(host_path, "rb");
    if (!fp) {
        return -errno;
    }

    char lfs_path[PATH_MAX];
    int res = build_lfs_path(lfs_path, sizeof(lfs_path), relative);
    if (res < 0) {
        fclose(fp);
        return res;
    }

    lfs_file_t file;
    res = lfs_file_open(lfs, &file, lfs_path, LFS_O_CREAT | LFS_O_TRUNC | LFS_O_WRONLY);
    if (res < 0) {
        fclose(fp);
        return res;
    }

    uint8_t buffer[HOST_COPY_BUFFER_SIZE];
    while (true) {
        size_t read = fread(buffer, 1, sizeof(buffer), fp);
        if (read > 0) {
            int wrote = lfs_file_write(lfs, &file, buffer, read);
            if (wrote < 0) {
                res = wrote;
                break;
            }
            if ((size_t)wrote != read) {
                res = LFS_ERR_IO;
                break;
            }
        }

        if (read < sizeof(buffer)) {
            if (feof(fp)) {
                break;
            }
            if (ferror(fp)) {
                res = -errno;
                break;
            }
        }
    }

    int close_res = lfs_file_close(lfs, &file);
    if (close_res < 0 && res == 0) {
        res = close_res;
    }

    fclose(fp);

    if (verbose && res == 0) {
        fprintf(stderr, "copied %s -> %s\n", host_path, lfs_path);
    }

    return res;
}

static int copy_directory(lfs_t *lfs, const char *host_base, const char *relative, bool verbose)
{
    char host_path[PATH_MAX];
    int res = join_path(host_path, sizeof(host_path), host_base, relative);
    if (res < 0) {
        return res;
    }

    DIR *dir = opendir(host_path);
    if (!dir) {
        return -errno;
    }

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }

        char child_relative[PATH_MAX];
        int needed;
        if (relative[0] == '\0') {
            needed = snprintf(child_relative, sizeof(child_relative), "%s", entry->d_name);
        } else {
            needed = snprintf(child_relative, sizeof(child_relative), "%s/%s", relative, entry->d_name);
        }
        if (needed < 0 || (size_t)needed >= sizeof(child_relative)) {
            closedir(dir);
            return -LFS_ERR_NOMEM;
        }

        char child_host[PATH_MAX];
        res = join_path(child_host, sizeof(child_host), host_base, child_relative);
        if (res < 0) {
            closedir(dir);
            return res;
        }

        struct stat st;
        if (stat(child_host, &st) != 0) {
            closedir(dir);
            return -errno;
        }

        if (S_ISDIR(st.st_mode)) {
            char lfs_dir_path[PATH_MAX];
            res = build_lfs_path(lfs_dir_path, sizeof(lfs_dir_path), child_relative);
            if (res < 0) {
                closedir(dir);
                return res;
            }

            if (verbose) {
                fprintf(stderr, "mkdir %s\n", lfs_dir_path);
            }

            int mkdir_res = lfs_mkdir(lfs, lfs_dir_path);
            if (mkdir_res < 0 && mkdir_res != LFS_ERR_EXIST) {
                closedir(dir);
                return mkdir_res;
            }

            res = copy_directory(lfs, host_base, child_relative, verbose);
            if (res < 0) {
                closedir(dir);
                return res;
            }
        } else if (S_ISREG(st.st_mode)) {
            res = copy_file(lfs, child_host, child_relative, verbose);
            if (res < 0) {
                closedir(dir);
                return res;
            }
        } else if (verbose) {
            fprintf(stderr, "skip %s (not a regular file or directory)\n", child_host);
        }
    }

    closedir(dir);
    return 0;
}

static bool parse_size(const char *value, size_t *output)
{
    char *end = NULL;
    errno = 0;
    unsigned long long parsed = strtoull(value, &end, 0);
    if (errno != 0 || end == value || *end != '\0') {
        return false;
    }

    *output = (size_t)parsed;
    return true;
}

static void print_usage(FILE *stream)
{
    fprintf(stream,
            "Usage: littlefs_mkimage create <source_dir> <output_image> "
            "--fs-size=<size> --name-max=<len> --block-size=<size> [options]\n"
            "Options:\n"
            "  -v, --verbose              Print progress\n"
            "  --read-size=<size>         LittleFS read buffer\n"
            "  --prog-size=<size>         LittleFS prog buffer\n"
            "  --cache-size=<size>        LittleFS cache buffer\n"
            "  --lookahead-size=<size>    LittleFS lookahead buffer\n"
            "  --block-cycles=<cycles>    LittleFS block cycles\n");
}

int main(int argc, char **argv)
{
    if (argc < 4) {
        print_usage(stderr);
        return 1;
    }

    if (strcmp(argv[1], "create") != 0) {
        print_usage(stderr);
        return 1;
    }

    const char *src_dir = argv[2];
    const char *output = argv[3];

    bool verbose = false;
    size_t fs_size = 0;
    size_t block_size = 4096;
    size_t name_max = 64;
    size_t read_size = 16;
    size_t prog_size = 16;
    size_t cache_size = 0;
    size_t lookahead_size = 0;
    size_t block_cycles = 500;

    for (int i = 4; i < argc; ++i) {
        const char *arg = argv[i];

        if (strcmp(arg, "-v") == 0 || strcmp(arg, "--verbose") == 0) {
            verbose = true;
            continue;
        }

        if (strncmp(arg, "--fs-size=", 10) == 0) {
            if (!parse_size(arg + 10, &fs_size)) {
                fprintf(stderr, "Invalid fs-size: %s\n", arg);
                return 1;
            }
            continue;
        }

        if (strncmp(arg, "--block-size=", 13) == 0) {
            if (!parse_size(arg + 13, &block_size)) {
                fprintf(stderr, "Invalid block-size: %s\n", arg);
                return 1;
            }
            continue;
        }

        if (strncmp(arg, "--name-max=", 11) == 0) {
            if (!parse_size(arg + 11, &name_max)) {
                fprintf(stderr, "Invalid name-max: %s\n", arg);
                return 1;
            }
            continue;
        }

        if (strncmp(arg, "--read-size=", 12) == 0) {
            if (!parse_size(arg + 12, &read_size)) {
                fprintf(stderr, "Invalid read-size: %s\n", arg);
                return 1;
            }
            continue;
        }

        if (strncmp(arg, "--prog-size=", 12) == 0) {
            if (!parse_size(arg + 12, &prog_size)) {
                fprintf(stderr, "Invalid prog-size: %s\n", arg);
                return 1;
            }
            continue;
        }

        if (strncmp(arg, "--cache-size=", 13) == 0) {
            if (!parse_size(arg + 13, &cache_size)) {
                fprintf(stderr, "Invalid cache-size: %s\n", arg);
                return 1;
            }
            continue;
        }

        if (strncmp(arg, "--lookahead-size=", 17) == 0) {
            if (!parse_size(arg + 17, &lookahead_size)) {
                fprintf(stderr, "Invalid lookahead-size: %s\n", arg);
                return 1;
            }
            continue;
        }

        if (strncmp(arg, "--block-cycles=", 16) == 0) {
            if (!parse_size(arg + 16, &block_cycles)) {
                fprintf(stderr, "Invalid block-cycles: %s\n", arg);
                return 1;
            }
            continue;
        }

        fprintf(stderr, "Unknown option: %s\n", arg);
        return 1;
    }

    if (fs_size == 0) {
        fprintf(stderr, "Missing --fs-size option\n");
        return 1;
    }

    if (block_size == 0) {
        fprintf(stderr, "Block size must be greater than 0\n");
        return 1;
    }

    if (fs_size % block_size != 0) {
        fprintf(stderr, "fs-size must be a multiple of block-size\n");
        return 1;
    }

    size_t block_count = fs_size / block_size;
    if (block_count == 0) {
        fprintf(stderr, "block count must be non-zero\n");
        return 1;
    }

    if (read_size == 0) {
        read_size = 16;
    }

    if (prog_size == 0) {
        prog_size = 16;
    }

    if (cache_size == 0) {
        cache_size = block_size;
    }

    if (lookahead_size == 0) {
        lookahead_size = (block_count + 7) / 8;
        if (lookahead_size == 0) {
            lookahead_size = 1;
        }
    }

    image_context_t image = {0};
    image.size = fs_size;
    image.buffer = malloc(fs_size);
    if (!image.buffer) {
        fprintf(stderr, "Failed to allocate %" PRIu64 " bytes for image\n", (uint64_t)fs_size);
        return 1;
    }
    memset(image.buffer, 0xFF, fs_size);

    struct lfs_config cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.context = &image;
    cfg.read = block_device_read;
    cfg.prog = block_device_prog;
    cfg.erase = block_device_erase;
    cfg.sync = block_device_sync;
    cfg.read_size = read_size;
    cfg.prog_size = prog_size;
    cfg.block_size = block_size;
    cfg.block_count = block_count;
    cfg.block_cycles = block_cycles;
    cfg.cache_size = cache_size;
    cfg.lookahead_size = lookahead_size;
    cfg.name_max = name_max;

    uint8_t *read_buffer = NULL;
    uint8_t *prog_buffer = NULL;
    uint8_t *lookahead_buffer = NULL;

    read_buffer = malloc(cache_size);
    prog_buffer = malloc(cache_size);
    lookahead_buffer = malloc(lookahead_size);

    if (!read_buffer || !prog_buffer || !lookahead_buffer) {
        fprintf(stderr, "Failed to allocate buffers for LittleFS\n");
        free(read_buffer);
        free(prog_buffer);
        free(lookahead_buffer);
        free(image.buffer);
        return 1;
    }

    cfg.read_buffer = read_buffer;
    cfg.prog_buffer = prog_buffer;
    cfg.lookahead_buffer = lookahead_buffer;

    lfs_t lfs;
    int err = 0;
    bool mounted = false;
    FILE *fp = NULL;

    err = lfs_format(&lfs, &cfg);
    if (err) {
        fprintf(stderr, "lfs_format failed (%d)\n", err);
        err = (err < 0) ? err : -err;
        goto cleanup;
    }

    err = lfs_mount(&lfs, &cfg);
    if (err) {
        fprintf(stderr, "lfs_mount failed (%d)\n", err);
        err = (err < 0) ? err : -err;
        goto cleanup;
    }
    mounted = true;

    err = copy_directory(&lfs, src_dir, "", verbose);
    if (err) {
        fprintf(stderr, "Failed to populate image (%d)\n", err);
        goto cleanup;
    }

    if (lfs_unmount(&lfs) < 0) {
        fprintf(stderr, "lfs_unmount failed\n");
        err = -1;
        goto cleanup;
    }
    mounted = false;

    fp = fopen(output, "wb");
    if (!fp) {
        fprintf(stderr, "Failed to open %s (%s)\n", output, strerror(errno));
        err = -errno;
        goto cleanup;
    }

    size_t written = fwrite(image.buffer, 1, image.size, fp);
    if (written != image.size) {
        int write_errno = ferror(fp) ? errno : EIO;
        fprintf(stderr, "Failed to write image file: %s\n", strerror(write_errno));
        err = -write_errno;
    }

cleanup:
    if (fp) {
        fclose(fp);
    }
    if (mounted) {
        lfs_unmount(&lfs);
    }
    free(read_buffer);
    free(prog_buffer);
    free(lookahead_buffer);
    free(image.buffer);
    return err ? 1 : 0;
}
