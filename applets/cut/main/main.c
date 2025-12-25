#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <stdbool.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

typedef struct {
    long start;
    long end; /* -1 for open-ended */
} range_t;

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

static void print_help(void)
{
    printf("usage: cut OPTION... [FILE]...\n");
    printf("  -b LIST       select only these bytes\n");
    printf("  -c LIST       select only these characters\n");
    printf("  -f LIST       select only these fields\n");
    printf("  -d DELIM      use DELIM instead of TAB for fields\n");
    printf("  -s            do not print lines without delimiters\n");
    printf("      --help    display this help and exit\n");
    printf("      --version output version information and exit\n");
    printf("LIST supports N, N-M, N-, -M separated by commas.\n");
}

static void print_version(void)
{
    printf("cut (%s)\n", g_version);
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

static int parse_ranges(const char *list, range_t *ranges, size_t cap, size_t *out_n)
{
    *out_n = 0;
    if (!list || *list == '\0') {
        return -1;
    }
    const char *p = list;
    while (*p) {
        if (*out_n >= cap) {
            return -1;
        }
        long start = -1;
        long end = -1;
        if (*p == '-') {
            start = 1;
            p++;
            char *e = NULL;
            end = strtol(p, &e, 10);
            if (e == p || end < 1) {
                return -1;
            }
            p = e;
        } else {
            char *e = NULL;
            start = strtol(p, &e, 10);
            if (e == p || start < 1) {
                return -1;
            }
            p = e;
            if (*p == '-') {
                p++;
                if (*p == '\0' || *p == ',') {
                    end = -1;
                } else {
                    e = NULL;
                    end = strtol(p, &e, 10);
                    if (e == p || end < start) {
                        return -1;
                    }
                    p = e;
                }
            } else {
                end = start;
            }
        }
        ranges[*out_n].start = start;
        ranges[*out_n].end = end;
        (*out_n)++;
        if (*p == ',') {
            p++;
            continue;
        }
        if (*p != '\0') {
            return -1;
        }
    }
    return 0;
}

static bool selected(long idx, const range_t *ranges, size_t n)
{
    for (size_t i = 0; i < n; ++i) {
        long a = ranges[i].start;
        long b = ranges[i].end;
        if (idx < a) {
            continue;
        }
        if (b < 0 || idx <= b) {
            return true;
        }
    }
    return false;
}

static int cut_stream_bytes(const range_t *ranges, size_t n)
{
    char buf[256];
    long pos = 0;
    while (1) {
        ssize_t r = read(STDIN_FILENO, buf, sizeof(buf));
        if (r < 0) {
            return -1;
        }
        if (r == 0) {
            return 0;
        }
        for (ssize_t i = 0; i < r; ++i) {
            pos++;
            if (buf[i] == '\n') {
                pos = 0;
                if (write_all(STDOUT_FILENO, "\n", 1) != 0) {
                    return -1;
                }
                continue;
            }
            if (selected(pos, ranges, n)) {
                if (write_all(STDOUT_FILENO, &buf[i], 1) != 0) {
                    return -1;
                }
            }
        }
    }
}

static int cut_stream_fields(const range_t *ranges, size_t n, char delim, bool suppress_no_delim)
{
    char buf[256];
    char *line = NULL;
    size_t len = 0;
    size_t cap = 0;

    while (1) {
        ssize_t r = read(STDIN_FILENO, buf, sizeof(buf));
        if (r < 0) {
            goto fail;
        }
        if (r == 0) {
            break;
        }
        for (ssize_t i = 0; i < r; ++i) {
            if (len + 2 > cap) {
                size_t next = cap ? cap * 2 : 128;
                while (next < len + 2) {
                    next *= 2;
                }
                char *tmp = (char *)realloc(line, next);
                if (!tmp) {
                    errno = ENOMEM;
                    goto fail;
                }
                line = tmp;
                cap = next;
            }
            line[len++] = buf[i];
            if (buf[i] == '\n') {
                line[len] = '\0';

                bool has_delim = (memchr(line, delim, len) != NULL);
                if (!has_delim && suppress_no_delim) {
                    len = 0;
                    continue;
                }

                long field = 1;
                bool first_out = true;
                size_t start = 0;
                for (size_t j = 0; j <= len; ++j) {
                    bool end_field = (j == len) || (line[j] == delim) || (line[j] == '\n') || (line[j] == '\0');
                    if (!end_field) {
                        continue;
                    }
                    if (selected(field, ranges, n)) {
                        if (!first_out) {
                            if (write_all(STDOUT_FILENO, &delim, 1) != 0) {
                                goto fail;
                            }
                        }
                        if (j > start) {
                            if (write_all(STDOUT_FILENO, line + start, j - start) != 0) {
                                goto fail;
                            }
                        }
                        first_out = false;
                    }
                    if (line[j] == '\n') {
                        break;
                    }
                    field++;
                    start = j + 1;
                }
                if (write_all(STDOUT_FILENO, "\n", 1) != 0) {
                    goto fail;
                }
                len = 0;
            }
        }
    }

    free(line);
    return 0;

fail:
    free(line);
    return -1;
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

    const char *list = NULL;
    bool fields = false;
    char delim = '\t';
    bool suppress_no_delim = false;

    int opt;
    while ((opt = getopt(argc, argv, "b:c:f:d:s")) != -1) {
        switch (opt) {
        case 'b':
        case 'c':
            list = optarg;
            fields = false;
            break;
        case 'f':
            list = optarg;
            fields = true;
            break;
        case 'd':
            if (!optarg || !optarg[0] || optarg[1]) {
                eprintf("cut: invalid delimiter\n");
                return 1;
            }
            delim = optarg[0];
            break;
        case 's':
            suppress_no_delim = true;
            break;
        default:
            eprintf("usage: cut (-b LIST|-c LIST|-f LIST) [FILE...]\n");
            return 1;
        }
    }

    if (!list) {
        eprintf("cut: you must specify a list of bytes, characters, or fields\n");
        return 1;
    }

    range_t ranges[64];
    size_t nr = 0;
    if (parse_ranges(list, ranges, sizeof(ranges) / sizeof(ranges[0]), &nr) != 0) {
        eprintf("cut: invalid list: %s\n", list);
        return 1;
    }

    int failed = 0;
    if (optind >= argc) {
        int rc = fields ? cut_stream_fields(ranges, nr, delim, suppress_no_delim)
                        : cut_stream_bytes(ranges, nr);
        if (rc != 0) {
            eprintf("cut: %s\n", strerror(errno));
            return 1;
        }
        return 0;
    }

    for (int i = optind; i < argc; ++i) {
        const char *path = argv[i];
        if (!path) {
            continue;
        }
        int fd = (strcmp(path, "-") == 0) ? STDIN_FILENO : open(path, O_RDONLY);
        if (fd < 0) {
            eprintf("cut: %s: %s\n", path, strerror(errno));
            failed = 1;
            continue;
        }
        int saved = dup(STDIN_FILENO);
        if (saved < 0) {
            if (fd != STDIN_FILENO) {
                (void)close(fd);
            }
            eprintf("cut: dup: %s\n", strerror(errno));
            return 1;
        }
        if (fd != STDIN_FILENO) {
            if (dup2(fd, STDIN_FILENO) < 0) {
                (void)close(saved);
                (void)close(fd);
                eprintf("cut: dup2: %s\n", strerror(errno));
                return 1;
            }
        }
        int rc = fields ? cut_stream_fields(ranges, nr, delim, suppress_no_delim)
                        : cut_stream_bytes(ranges, nr);
        if (rc != 0) {
            eprintf("cut: %s: %s\n", path, strerror(errno));
            failed = 1;
        }
        (void)dup2(saved, STDIN_FILENO);
        (void)close(saved);
        if (fd != STDIN_FILENO) {
            (void)close(fd);
        }
    }
    return failed ? 1 : 0;
}
