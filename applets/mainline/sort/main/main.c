#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <stdbool.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* BSD reference: FreeBSD sort(1). */

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
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }
        if (w == 0) {
            errno = EIO;
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

static int grow_buffer(void **buf, size_t *cap, size_t needed, size_t item_size)
{
    /* Guard against size_t overflow when growing buffers. */
    if (*cap >= needed) {
        return 0;
    }
    size_t next = *cap ? *cap : 128;
    while (next < needed) {
        if (next > SIZE_MAX / 2) {
            errno = ENOMEM;
            return -1;
        }
        next *= 2;
    }
    if (item_size > 0 && next > SIZE_MAX / item_size) {
        errno = ENOMEM;
        return -1;
    }
    void *tmp = realloc(*buf, next * item_size);
    if (!tmp) {
        errno = ENOMEM;
        return -1;
    }
    *buf = tmp;
    *cap = next;
    return 0;
}

static int read_lines_from_fd(int fd, char ***out_lines, size_t *out_count, size_t *out_cap)
{
    char buf[256];
    char *line = NULL;
    size_t len = 0;
    size_t cap = 0;

    while (1) {
        ssize_t r = read(fd, buf, sizeof(buf));
        if (r < 0) {
            if (errno == EINTR) {
                continue;
            }
            free(line);
            return -1;
        }
        if (r == 0) {
            break;
        }
        for (ssize_t i = 0; i < r; ++i) {
            if (len + 2 > cap) {
                if (grow_buffer((void **)&line, &cap, len + 2, 1) != 0) {
                    free(line);
                    return -1;
                }
            }
            line[len++] = buf[i];
            if (buf[i] == '\n') {
                line[len] = '\0';
                char *copy = xstrdup(line);
                if (!copy) {
                    free(line);
                    errno = ENOMEM;
                    return -1;
                }
                if (*out_count == *out_cap) {
                    if (grow_buffer((void **)out_lines, out_cap, *out_count + 1, sizeof(char *)) != 0) {
                        free(copy);
                        free(line);
                        return -1;
                    }
                }
                (*out_lines)[(*out_count)++] = copy;
                len = 0;
            }
        }
    }

    if (len > 0) {
        line[len] = '\0';
        char *copy = xstrdup(line);
        if (!copy) {
            free(line);
            errno = ENOMEM;
            return -1;
        }
        if (*out_count == *out_cap) {
            if (grow_buffer((void **)out_lines, out_cap, *out_count + 1, sizeof(char *)) != 0) {
                free(copy);
                free(line);
                return -1;
            }
        }
        (*out_lines)[(*out_count)++] = copy;
    }

    free(line);
    return 0;
}

typedef struct {
    bool reverse;
} sort_opts_t;

static sort_opts_t g_sort_opts;

static int cmp_lines(const void *a, const void *b)
{
    const char *sa = *(const char *const *)a;
    const char *sb = *(const char *const *)b;
    int rc = strcmp(sa, sb);
    return g_sort_opts.reverse ? -rc : rc;
}

static void print_help(void)
{
    printf("usage: sort [OPTION]... [FILE]...\n");
    printf("  -r           reverse the result of comparisons\n");
    printf("  -u           output only the first of an equal run\n");
    printf("  -o FILE      write result to FILE\n");
    printf("      --help   display this help and exit\n");
    printf("      --version output version information and exit\n");
    printf("note: only basic in-memory lexicographic sort is implemented.\n");
}

static void print_version(void)
{
    printf("sort (%s)\n", g_version);
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

    sort_opts_t opts = {0};
    bool unique = false;
    const char *out_path = NULL;

    int opt;
    while ((opt = getopt(argc, argv, "ruo:")) != -1) {
        switch (opt) {
        case 'r':
            opts.reverse = true;
            break;
        case 'u':
            unique = true;
            break;
        case 'o':
            out_path = optarg;
            break;
        default:
            eprintf("usage: sort [-r] [-u] [-o FILE] [FILE...]\n");
            return 1;
        }
    }

    char **lines = NULL;
    size_t count = 0;
    size_t cap = 0;
    int failed = 0;

    if (optind >= argc) {
        if (read_lines_from_fd(STDIN_FILENO, &lines, &count, &cap) != 0) {
            eprintf("sort: read: %s\n", strerror(errno));
            return 1;
        }
    } else {
        for (int i = optind; i < argc; ++i) {
            const char *path = argv[i];
            if (!path) {
                continue;
            }
            int fd = (strcmp(path, "-") == 0) ? STDIN_FILENO : open(path, O_RDONLY);
            if (fd < 0) {
                eprintf("sort: %s: %s\n", path, strerror(errno));
                failed = 1;
                continue;
            }
            if (read_lines_from_fd(fd, &lines, &count, &cap) != 0) {
                eprintf("sort: %s: %s\n", path, strerror(errno));
                failed = 1;
            }
            if (fd != STDIN_FILENO) {
                (void)close(fd);
            }
        }
    }

    if (count > 0) {
        g_sort_opts = opts;
        qsort(lines, count, sizeof(char *), cmp_lines);
    }

    int out_fd = STDOUT_FILENO;
    if (out_path) {
        out_fd = open(out_path, O_WRONLY | O_CREAT | O_TRUNC, 0666);
        if (out_fd < 0) {
            eprintf("sort: %s: %s\n", out_path, strerror(errno));
            failed = 1;
            goto done;
        }
    }

    const char *prev = NULL;
    for (size_t i = 0; i < count; ++i) {
        const char *s = lines[i] ? lines[i] : "";
        if (unique && prev && strcmp(prev, s) == 0) {
            continue;
        }
        if (write_all(out_fd, s, strlen(s)) != 0) {
            eprintf("sort: write: %s\n", strerror(errno));
            failed = 1;
            break;
        }
        prev = s;
    }

done:
    if (out_path && out_fd >= 0 && out_fd != STDOUT_FILENO) {
        (void)close(out_fd);
    }
    for (size_t i = 0; i < count; ++i) {
        free(lines[i]);
    }
    free(lines);
    return failed ? 1 : 0;
}
