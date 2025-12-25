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

static char *xstrdup(const char *s)
{
    size_t len = strlen(s) + 1;
    char *out = (char *)malloc(len);
    if (!out) {
        return NULL;
    }
    memcpy(out, s, len);
    return out;
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

static int tail_fd(int fd, long nlines)
{
    if (nlines <= 0) {
        return 0;
    }

    char **ring = (char **)calloc((size_t)nlines, sizeof(char *));
    if (!ring) {
        errno = ENOMEM;
        return -1;
    }
    long count = 0;
    long idx = 0;

    char buf[256];
    char *line = NULL;
    size_t line_len = 0;
    size_t line_cap = 0;

    while (1) {
        ssize_t r = read(fd, buf, sizeof(buf));
        if (r < 0) {
            goto fail;
        }
        if (r == 0) {
            break;
        }
        for (ssize_t i = 0; i < r; ++i) {
            if (line_len + 2 > line_cap) {
                size_t next = line_cap ? line_cap * 2 : 128;
                while (next < line_len + 2) {
                    next *= 2;
                }
                char *tmp = (char *)realloc(line, next);
                if (!tmp) {
                    errno = ENOMEM;
                    goto fail;
                }
                line = tmp;
                line_cap = next;
            }
            line[line_len++] = buf[i];
            if (buf[i] == '\n') {
                line[line_len] = '\0';
                free(ring[idx]);
                ring[idx] = xstrdup(line);
                if (!ring[idx]) {
                    errno = ENOMEM;
                    goto fail;
                }
                idx = (idx + 1) % nlines;
                if (count < nlines) {
                    count++;
                }
                line_len = 0;
            }
        }
    }

    if (line_len > 0) {
        if (line_len + 1 > line_cap) {
            char *tmp = (char *)realloc(line, line_len + 1);
            if (!tmp) {
                errno = ENOMEM;
                goto fail;
            }
            line = tmp;
            line_cap = line_len + 1;
        }
        line[line_len] = '\0';
        free(ring[idx]);
        ring[idx] = xstrdup(line);
        if (!ring[idx]) {
            errno = ENOMEM;
            goto fail;
        }
        idx = (idx + 1) % nlines;
        if (count < nlines) {
            count++;
        }
    }

    long start = (count < nlines) ? 0 : idx;
    for (long i = 0; i < count; ++i) {
        long pos = (start + i) % nlines;
        if (!ring[pos]) {
            continue;
        }
        if (write_all(STDOUT_FILENO, ring[pos], strlen(ring[pos])) != 0) {
            goto fail;
        }
    }

    for (long i = 0; i < nlines; ++i) {
        free(ring[i]);
    }
    free(ring);
    free(line);
    return 0;

fail:
    for (long i = 0; i < nlines; ++i) {
        free(ring[i]);
    }
    free(ring);
    free(line);
    return -1;
}

static int tail_path(const char *path, long nlines)
{
    if (path && strcmp(path, "-") == 0) {
        return tail_fd(STDIN_FILENO, nlines);
    }
    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        return -1;
    }
    int rc = tail_fd(fd, nlines);
    (void)close(fd);
    return rc;
}

static void print_help(void)
{
    printf("usage: tail [OPTION]... [FILE]...\n");
    printf("  -n N         output the last N lines (default 10)\n");
    printf("      --help   display this help and exit\n");
    printf("      --version output version information and exit\n");
}

static void print_version(void)
{
    printf("tail (%s)\n", g_version);
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

    long nlines = 10;
    int opt;
    while ((opt = getopt(argc, argv, "n:")) != -1) {
        switch (opt) {
        case 'n':
            if (parse_positive(optarg, &nlines) != 0) {
                eprintf("tail: invalid number of lines: %s\n", optarg ? optarg : "");
                return 1;
            }
            break;
        default:
            eprintf("usage: tail [-n N] [FILE...]\n");
            return 1;
        }
    }

    if (optind >= argc) {
        if (tail_fd(STDIN_FILENO, nlines) != 0) {
            eprintf("tail: read/write: %s\n", strerror(errno));
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
        if (tail_path(path, nlines) != 0) {
            eprintf("tail: %s: %s\n", path, strerror(errno));
            failed = 1;
        }
    }
    return failed ? 1 : 0;
}

