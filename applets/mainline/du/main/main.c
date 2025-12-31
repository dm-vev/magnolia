#include <dirent.h>
#include <errno.h>
#include <getopt.h>
#include <limits.h>
#include <stdbool.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

/*
 * BSD reference: FreeBSD du(1).
 * Behavior: supports -a and -s, reports 1 KiB blocks, and does not follow
 * symlinks during traversal.
 * Edge cases: long paths report ENAMETOOLONG; unreadable dirs propagate errno.
 */
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

static bool streq(const char *a, const char *b)
{
    return a && b && strcmp(a, b) == 0;
}

static char *join_path(const char *dir, const char *name)
{
    size_t dlen = strlen(dir);
    size_t nlen = strlen(name);
    bool need_slash = (dlen > 0 && dir[dlen - 1] != '/');
    /* Avoid size_t wrap when building long child paths. */
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
    if (!out) {
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

static unsigned long long blocks_1k(off_t size)
{
    if (size <= 0) {
        return 0;
    }
    /* Use div/mod to avoid signed overflow in size rounding. */
    uintmax_t bytes = (uintmax_t)size;
    uintmax_t blocks = bytes / 1024;
    if ((bytes % 1024) != 0) {
        if (blocks < UINTMAX_MAX) {
            blocks++;
        }
    }
    if (blocks > ULLONG_MAX) {
        return ULLONG_MAX;
    }
    return (unsigned long long)blocks;
}

static int du_walk(const char *path, bool all, bool summary, unsigned long long *out_blocks)
{
    struct stat st;
    if (lstat(path, &st) != 0) {
        return -1;
    }

    unsigned long long total = blocks_1k(st.st_size);
    if (S_ISDIR(st.st_mode)) {
        DIR *dir = opendir(path);
        if (!dir) {
            return -1;
        }
        struct dirent *ent;
        errno = 0;
        while ((ent = readdir(dir)) != NULL) {
            if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) {
                continue;
            }
            char *child = join_path(path, ent->d_name);
            if (!child) {
                /* Preserve errno from join_path in case closedir overwrites it. */
                int err = errno;
                (void)closedir(dir);
                errno = err;
                return -1;
            }
            unsigned long long child_blocks = 0;
            if (du_walk(child, all, summary, &child_blocks) != 0) {
                /* Preserve errno from child traversal before cleanup. */
                int err = errno;
                free(child);
                (void)closedir(dir);
                errno = err;
                return -1;
            }
            if (ULLONG_MAX - total < child_blocks) {
                total = ULLONG_MAX;
            } else {
                total += child_blocks;
            }
            if (all && !summary) {
                printf("%llu\t%s\n", child_blocks, child);
            }
            free(child);
        }
        if (errno != 0) {
            int err = errno;
            (void)closedir(dir);
            errno = err;
            return -1;
        }
        (void)closedir(dir);
    }

    *out_blocks = total;
    return 0;
}

static void print_help(void)
{
    printf("usage: du [OPTION]... [FILE]...\n");
    printf("  -a           write counts for all files, not just directories\n");
    printf("  -s           display only a total for each argument\n");
    printf("      --help   display this help and exit\n");
    printf("      --version output version information and exit\n");
}

static void print_version(void)
{
    printf("du (%s)\n", g_version);
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

    bool all = false;
    bool summary = false;
    int opt;
    while ((opt = getopt(argc, argv, "as")) != -1) {
        switch (opt) {
        case 'a':
            all = true;
            break;
        case 's':
            summary = true;
            break;
        default:
            eprintf("usage: du [-a] [-s] [FILE...]\n");
            return 1;
        }
    }

    int failed = 0;
    if (optind >= argc) {
        const char *path = ".";
        unsigned long long blocks = 0;
        if (du_walk(path, all, summary, &blocks) != 0) {
            eprintf("du: %s: %s\n", path, strerror(errno));
            return 1;
        }
        printf("%llu\t%s\n", blocks, path);
        return 0;
    }

    for (int i = optind; i < argc; ++i) {
        const char *path = argv[i];
        if (!path) {
            continue;
        }
        unsigned long long blocks = 0;
        if (du_walk(path, all, summary, &blocks) != 0) {
            eprintf("du: %s: %s\n", path, strerror(errno));
            failed = 1;
            continue;
        }
        printf("%llu\t%s\n", blocks, path);
    }
    return failed ? 1 : 0;
}
