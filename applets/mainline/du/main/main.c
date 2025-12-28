#include <dirent.h>
#include <errno.h>
#include <getopt.h>
#include <stdbool.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

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
    size_t total = dlen + (need_slash ? 1 : 0) + nlen + 1;
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
    return (unsigned long long)((size + 1023) / 1024);
}

static int du_walk(const char *path, bool all, bool summary, unsigned long long *out_blocks)
{
    struct stat st;
    if (stat(path, &st) != 0) {
        return -1;
    }

    unsigned long long total = blocks_1k(st.st_size);
    if (S_ISDIR(st.st_mode)) {
        DIR *dir = opendir(path);
        if (!dir) {
            return -1;
        }
        struct dirent *ent;
        while ((ent = readdir(dir)) != NULL) {
            if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) {
                continue;
            }
            char *child = join_path(path, ent->d_name);
            if (!child) {
                (void)closedir(dir);
                return -1;
            }
            unsigned long long child_blocks = 0;
            if (du_walk(child, all, summary, &child_blocks) != 0) {
                free(child);
                (void)closedir(dir);
                return -1;
            }
            total += child_blocks;
            if (all && !summary) {
                printf("%llu\t%s\n", child_blocks, child);
            }
            free(child);
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
