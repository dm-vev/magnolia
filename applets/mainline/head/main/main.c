#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <stdbool.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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

static int write_all(int fd, const void *buf, size_t len)
{
    const unsigned char *p = (const unsigned char *)buf;
    size_t off = 0;
    while (off < len) {
        ssize_t w = write(fd, p + off, len - off);
        if (w < 0) {
            return -1;
        }
        off += (size_t)w;
    }
    return 0;
}

static int copy_n_bytes(int fd, long limit)
{
    char buf[512];
    long remaining = limit;
    while (remaining > 0) {
        size_t want = sizeof(buf);
        if ((long)want > remaining) {
            want = (size_t)remaining;
        }
        ssize_t r = read(fd, buf, want);
        if (r < 0) {
            return -1;
        }
        if (r == 0) {
            return 0;
        }
        if (write_all(STDOUT_FILENO, buf, (size_t)r) != 0) {
            return -1;
        }
        remaining -= r;
    }
    return 0;
}

static int copy_n_lines(int fd, long limit)
{
    char buf[512];
    long lines = 0;
    while (lines < limit) {
        ssize_t r = read(fd, buf, sizeof(buf));
        if (r < 0) {
            return -1;
        }
        if (r == 0) {
            return 0;
        }

        size_t out_len = 0;
        for (ssize_t i = 0; i < r; ++i) {
            out_len++;
            if (buf[i] == '\n') {
                lines++;
                if (lines >= limit) {
                    break;
                }
            }
        }
        if (out_len > 0) {
            if (write_all(STDOUT_FILENO, buf, out_len) != 0) {
                return -1;
            }
        }
        if (lines >= limit) {
            return 0;
        }
    }
    return 0;
}

static void print_help(void)
{
    printf("usage: head [OPTION]... [FILE]...\n");
    printf("  -n N         print the first N lines (default 10)\n");
    printf("  -c N         print the first N bytes\n");
    printf("      --help   display this help and exit\n");
    printf("      --version output version information and exit\n");
}

static void print_version(void)
{
    printf("head (%s)\n", g_version);
}

static long parse_positive(const char *s, long *out)
{
    if (!s || *s == '\0') {
        return -1;
    }
    char *end = NULL;
    long v = strtol(s, &end, 10);
    if (end == s || *end != '\0' || v < 0) {
        return -1;
    }
    *out = v;
    return 0;
}

static int head_fd(int fd, bool by_bytes, long limit)
{
    if (limit < 0) {
        return 0;
    }
    return by_bytes ? copy_n_bytes(fd, limit) : copy_n_lines(fd, limit);
}

static int head_path(const char *path, bool by_bytes, long limit)
{
    if (path && strcmp(path, "-") == 0) {
        return head_fd(STDIN_FILENO, by_bytes, limit);
    }
    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        return -1;
    }
    int rc = head_fd(fd, by_bytes, limit);
    (void)close(fd);
    return rc;
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

    bool by_bytes = false;
    long limit = 10;
    int opt;
    while ((opt = getopt(argc, argv, "n:c:")) != -1) {
        switch (opt) {
        case 'n':
            by_bytes = false;
            if (parse_positive(optarg, &limit) != 0) {
                eprintf("head: invalid number of lines: %s\n", optarg ? optarg : "");
                return 1;
            }
            break;
        case 'c':
            by_bytes = true;
            if (parse_positive(optarg, &limit) != 0) {
                eprintf("head: invalid number of bytes: %s\n", optarg ? optarg : "");
                return 1;
            }
            break;
        default:
            eprintf("usage: head [-n N] [-c N] [FILE...]\n");
            return 1;
        }
    }

    if (optind >= argc) {
        if (head_fd(STDIN_FILENO, by_bytes, limit) != 0) {
            eprintf("head: read/write: %s\n", strerror(errno));
            return 1;
        }
        return 0;
    }

    int failed = 0;
    for (int i = optind; i < argc; ++i) {
        const char *path = argv[i];
        if (!path) {
            continue;
        }
        if (head_path(path, by_bytes, limit) != 0) {
            eprintf("head: %s: %s\n", path, strerror(errno));
            failed = 1;
        }
    }
    return failed ? 1 : 0;
}

