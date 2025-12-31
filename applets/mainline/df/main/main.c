#include <stdarg.h>
#include <dirent.h>
#include <errno.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "esp_partition.h"
#include "sdkconfig.h"

#ifndef CONFIG_MAGNOLIA_LITTLEFS_PARTITION_LABEL
#define CONFIG_MAGNOLIA_LITTLEFS_PARTITION_LABEL "vfs"
#endif
#ifndef CONFIG_MAGNOLIA_LITTLEFS_BLOCK_SIZE
#define CONFIG_MAGNOLIA_LITTLEFS_BLOCK_SIZE 0
#endif
#ifndef CONFIG_MAGNOLIA_LITTLEFS_BLOCK_COUNT
#define CONFIG_MAGNOLIA_LITTLEFS_BLOCK_COUNT 0
#endif

static const char *g_version = "Magnolia coreutils 0.1";

static void eprintf(const char *fmt, ...)
{
    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (n <= 0) {
        return;
    }
    size_t len = (size_t)n;
    if (len >= sizeof(buf)) {
        len = sizeof(buf) - 1;
    }
    (void)write(STDERR_FILENO, buf, len);
}

static int streq(const char *a, const char *b)
{
    return a && b && strcmp(a, b) == 0;
}

static void print_help(void)
{
    printf("usage: df [OPTION]... [FILE]...\n");
    printf("      --help    display this help and exit\n");
    printf("      --version output version information and exit\n");
}

static void print_version(void)
{
    printf("df (%s)\n", g_version);
}

static char *join_path(const char *dir, const char *name)
{
    size_t dlen = strlen(dir);
    size_t nlen = strlen(name);
    bool need_slash = (dlen > 0 && dir[dlen - 1] != '/');
    /* Guard against size_t overflow when building child paths. */
    size_t extra = nlen + 1;
    if (extra < nlen) {
        errno = ENAMETOOLONG;
        return NULL;
    }
    if (need_slash) {
        if (extra == SIZE_MAX) {
            errno = ENAMETOOLONG;
            return NULL;
        }
        extra += 1;
    }
    if (dlen > SIZE_MAX - extra) {
        errno = ENAMETOOLONG;
        return NULL;
    }
    size_t total = dlen + extra;
    char *out = (char *)malloc(total);
    if (out == NULL) {
        errno = ENOMEM;
        return NULL;
    }
    memcpy(out, dir, dlen);
    size_t off = dlen;
    if (need_slash) {
        out[off++] = '/';
    }
    memcpy(out + off, name, nlen);
    out[off + nlen] = '\0';
    return out;
}

static bool path_is_devfs(const char *path)
{
    if (path == NULL) {
        return false;
    }
    if (strcmp(path, "/dev") == 0) {
        return true;
    }
    return strncmp(path, "/dev/", 5) == 0;
}

static int usage_walk(const char *path, uint64_t *out_bytes)
{
    if (out_bytes == NULL) {
        errno = EINVAL;
        return -1;
    }

    struct stat st;
    if (lstat(path, &st) != 0) {
        return -1;
    }

    if (path_is_devfs(path)) {
        *out_bytes = 0;
        return 0;
    }

    /* Avoid following symlinks so traversal cannot loop on cycles. */
    if (S_ISLNK(st.st_mode)) {
        *out_bytes = (uint64_t)st.st_size;
        return 0;
    }

    uint64_t total = (uint64_t)st.st_size;
    if (S_ISDIR(st.st_mode)) {
        DIR *dir = opendir(path);
        if (dir == NULL) {
            return -1;
        }
        struct dirent *ent;
        errno = 0;
        while ((ent = readdir(dir)) != NULL) {
            if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) {
                continue;
            }
            char *child = join_path(path, ent->d_name);
            if (child == NULL) {
                (void)closedir(dir);
                return -1;
            }
            uint64_t child_bytes = 0;
            if (usage_walk(child, &child_bytes) != 0) {
                free(child);
                (void)closedir(dir);
                return -1;
            }
            /* Clamp on overflow to keep accounting monotonic. */
            if (UINT64_MAX - total < child_bytes) {
                total = UINT64_MAX;
            } else {
                total += child_bytes;
            }
            free(child);
        }
        if (errno != 0) {
            (void)closedir(dir);
            return -1;
        }
        (void)closedir(dir);
    }

    *out_bytes = total;
    return 0;
}

static bool littlefs_partition_info(const esp_partition_t **out_part,
                                    uint32_t *out_block_size,
                                    uint32_t *out_block_count)
{
    const char *label = NULL;
    if (CONFIG_MAGNOLIA_LITTLEFS_PARTITION_LABEL[0] != '\0') {
        label = CONFIG_MAGNOLIA_LITTLEFS_PARTITION_LABEL;
    }
    if (label == NULL) {
        errno = ENODEV;
        return false;
    }

    const esp_partition_t *part = esp_partition_find_first(
            ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, label);
    if (part == NULL) {
        errno = ENODEV;
        return false;
    }

    uint32_t erase_size = part->erase_size;
    uint32_t block_size = CONFIG_MAGNOLIA_LITTLEFS_BLOCK_SIZE;
    if (erase_size == 0) {
        erase_size = 4096;
    }
    if (block_size < erase_size || (block_size % erase_size) != 0) {
        block_size = erase_size;
    }

    uint32_t max_blocks = part->size / block_size;
    uint32_t cfg_blocks = CONFIG_MAGNOLIA_LITTLEFS_BLOCK_COUNT;
    uint32_t block_count = max_blocks;
    if (cfg_blocks > 0 && cfg_blocks < max_blocks) {
        block_count = cfg_blocks;
    } else if (cfg_blocks > max_blocks) {
        block_count = max_blocks;
    }

    *out_part = part;
    *out_block_size = block_size;
    *out_block_count = block_count;
    return true;
}

static uint64_t blocks_1k(uint64_t bytes)
{
    if (bytes == 0) {
        return 0;
    }
    return (bytes + 1023u) / 1024u;
}

static void print_df_line(const char *fs_name,
                          const char *mountpoint,
                          uint64_t total_bytes,
                          uint64_t used_bytes)
{
    uint64_t total_blocks = blocks_1k(total_bytes);
    uint64_t used_blocks = blocks_1k(used_bytes);
    uint64_t avail_blocks = (total_blocks > used_blocks) ? (total_blocks - used_blocks) : 0;
    unsigned int capacity = 0;
    if (total_blocks > 0) {
        capacity = (unsigned int)((used_blocks * 100u + (total_blocks / 2u)) / total_blocks);
        if (capacity > 100u) {
            capacity = 100u;
        }
    }

    printf("%-12s %12" PRIu64 " %12" PRIu64 " %12" PRIu64 " %3u%% %s\n",
           fs_name, total_blocks, used_blocks, avail_blocks, capacity, mountpoint);
}

int main(int argc, char **argv)
{
    for (int i = 1; i < argc; ++i) {
        if (streq(argv[i], "--help")) {
            print_help();
            return 0;
        }
        if (streq(argv[i], "--version")) {
            print_version();
            return 0;
        }
    }

    bool want_root = false;
    bool want_dev = false;
    bool saw_path = false;
    if (argc <= 1) {
        want_root = true;
    } else {
        bool in_paths = false;
        for (int i = 1; i < argc; ++i) {
            if (argv[i] == NULL) {
                continue;
            }
            if (!in_paths) {
                if (streq(argv[i], "--")) {
                    in_paths = true;
                    continue;
                }
                if (argv[i][0] == '-') {
                    eprintf("usage: df [OPTION]... [FILE]...\n");
                    return 1;
                }
            }
            saw_path = true;
            if (path_is_devfs(argv[i])) {
                want_dev = true;
            } else {
                want_root = true;
            }
        }
        if (!saw_path) {
            want_root = true;
        }
    }

    printf("Filesystem 1024-blocks        Used   Available Capacity Mounted on\n");

    int failed = 0;
    if (want_root) {
        const esp_partition_t *part = NULL;
        uint32_t block_size = 0;
        uint32_t block_count = 0;
        if (!littlefs_partition_info(&part, &block_size, &block_count)) {
            eprintf("df: unable to locate littlefs partition\n");
            return 1;
        }

        uint64_t total_bytes = (uint64_t)block_size * (uint64_t)block_count;
        uint64_t used_bytes = 0;
        if (usage_walk("/", &used_bytes) != 0) {
            eprintf("df: /: %s\n", strerror(errno));
            failed = 1;
        }
        if (used_bytes > total_bytes) {
            used_bytes = total_bytes;
        }

        const char *fs_name = (part->label[0] != '\0') ? part->label : "littlefs";
        print_df_line(fs_name, "/", total_bytes, used_bytes);
    }

    if (want_dev) {
        print_df_line("devfs", "/dev", 0, 0);
    }

    return failed ? 1 : 0;
}
