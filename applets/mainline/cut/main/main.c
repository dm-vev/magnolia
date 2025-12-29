#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <limits.h>
#include <stdbool.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

typedef struct {
    size_t start;
    size_t end; /* SIZE_MAX for open-ended ranges. */
} range_t;

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

static void usage(void)
{
    eprintf("usage: cut -b list [-n] [file ...]\n");
    eprintf("       cut -c list [file ...]\n");
    eprintf("       cut -f list [-d delim] [-s] [file ...]\n");
}

static ssize_t read_retry(int fd, void *buf, size_t len)
{
    while (1) {
        ssize_t r = read(fd, buf, len);
        if (r < 0 && errno == EINTR) {
            continue;
        }
        return r;
    }
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

static int parse_number(const char *s, char **end, size_t *out)
{
    if (!s || *s == '\0') {
        return -1;
    }
    if (*s == '+' || *s == '-') {
        return -1;
    }
    errno = 0;
    char *e = NULL;
    unsigned long long v = strtoull(s, &e, 10);
    if (errno == ERANGE || e == s) {
        return -1;
    }
    if (v == 0 || v > SIZE_MAX) {
        return -1;
    }
    *end = e;
    *out = (size_t)v;
    return 0;
}

static int append_range(range_t **ranges, size_t *count, size_t *cap, size_t start, size_t end)
{
    if (*count == *cap) {
        size_t next = *cap ? (*cap * 2u) : 8u;
        if (next < *cap || next > SIZE_MAX / sizeof(range_t)) {
            errno = EOVERFLOW;
            return -1;
        }
        range_t *tmp = (range_t *)realloc(*ranges, next * sizeof(range_t));
        if (!tmp) {
            errno = ENOMEM;
            return -1;
        }
        *ranges = tmp;
        *cap = next;
    }
    (*ranges)[*count].start = start;
    (*ranges)[*count].end = end;
    (*count)++;
    return 0;
}

static int parse_ranges(const char *list, range_t **out_ranges, size_t *out_n)
{
    if (!list || *list == '\0') {
        errno = EINVAL;
        return -1;
    }

    range_t *ranges = NULL;
    size_t count = 0;
    size_t cap = 0;

    const char *p = list;
    while (*p != '\0') {
        size_t start = 0;
        size_t end = 0;
        if (*p == '-') {
            p++;
            char *e = NULL;
            if (parse_number(p, &e, &end) != 0) {
                goto invalid;
            }
            start = 1;
            p = e;
        } else {
            char *e = NULL;
            if (parse_number(p, &e, &start) != 0) {
                goto invalid;
            }
            p = e;
            if (*p == '-') {
                p++;
                if (*p == '\0' || *p == ',') {
                    end = SIZE_MAX;
                } else {
                    if (parse_number(p, &e, &end) != 0) {
                        goto invalid;
                    }
                    if (end < start) {
                        goto invalid;
                    }
                    p = e;
                }
            } else {
                end = start;
            }
        }

        if (append_range(&ranges, &count, &cap, start, end) != 0) {
            free(ranges);
            return -1;
        }

        if (*p == ',') {
            p++;
            if (*p == '\0') {
                goto invalid;
            }
            continue;
        }
        if (*p != '\0') {
            goto invalid;
        }
    }

    *out_ranges = ranges;
    *out_n = count;
    return 0;

invalid:
    free(ranges);
    errno = EINVAL;
    return -1;
}

static bool selected(size_t idx, const range_t *ranges, size_t n)
{
    for (size_t i = 0; i < n; ++i) {
        size_t a = ranges[i].start;
        size_t b = ranges[i].end;
        if (idx < a) {
            continue;
        }
        if (b == SIZE_MAX || idx <= b) {
            return true;
        }
    }
    return false;
}

static int cut_stream_bytes(int fd, const range_t *ranges, size_t n)
{
    char buf[256];
    size_t pos = 0;

    while (1) {
        ssize_t r = read_retry(fd, buf, sizeof(buf));
        if (r < 0) {
            return -1;
        }
        if (r == 0) {
            return 0;
        }
        for (ssize_t i = 0; i < r; ++i) {
            unsigned char ch = (unsigned char)buf[i];
            if (ch == '\n') {
                pos = 0;
                if (write_all(STDOUT_FILENO, "\n", 1) != 0) {
                    return -1;
                }
                continue;
            }
            if (pos == SIZE_MAX) {
                errno = EOVERFLOW;
                return -1;
            }
            pos++;
            if (selected(pos, ranges, n)) {
                if (write_all(STDOUT_FILENO, &ch, 1) != 0) {
                    return -1;
                }
            }
        }
    }
}

static int cut_fields_line(const char *line, size_t len, const range_t *ranges, size_t n,
                           char delim, bool suppress_no_delim, bool had_newline)
{
    bool has_delim = (memchr(line, delim, len) != NULL);
    if (!has_delim) {
        if (suppress_no_delim) {
            return 0;
        }
        if (len > 0 && write_all(STDOUT_FILENO, line, len) != 0) {
            return -1;
        }
        if (had_newline && write_all(STDOUT_FILENO, "\n", 1) != 0) {
            return -1;
        }
        return 0;
    }

    size_t field = 1;
    bool first_out = true;
    size_t start = 0;
    for (size_t j = 0; j <= len; ++j) {
        bool end_field = (j == len) || (line[j] == delim);
        if (!end_field) {
            continue;
        }
        if (selected(field, ranges, n)) {
            if (!first_out) {
                if (write_all(STDOUT_FILENO, &delim, 1) != 0) {
                    return -1;
                }
            }
            if (j > start) {
                if (write_all(STDOUT_FILENO, line + start, j - start) != 0) {
                    return -1;
                }
            }
            first_out = false;
        }
        field++;
        start = j + 1;
    }

    if (had_newline && write_all(STDOUT_FILENO, "\n", 1) != 0) {
        return -1;
    }
    return 0;
}

static int cut_stream_fields(int fd, const range_t *ranges, size_t n, char delim,
                             bool suppress_no_delim)
{
    char buf[256];
    char *line = NULL;
    size_t len = 0;
    size_t cap = 0;

    while (1) {
        ssize_t r = read_retry(fd, buf, sizeof(buf));
        if (r < 0) {
            goto fail;
        }
        if (r == 0) {
            break;
        }
        for (ssize_t i = 0; i < r; ++i) {
            char ch = buf[i];
            if (ch == '\n') {
                if (cut_fields_line(line, len, ranges, n, delim, suppress_no_delim, true) != 0) {
                    goto fail;
                }
                len = 0;
                continue;
            }
            if (len + 1 > cap) {
                size_t next = cap ? (cap * 2u) : 128u;
                while (next < len + 1) {
                    if (next > SIZE_MAX / 2u) {
                        errno = EOVERFLOW;
                        goto fail;
                    }
                    next *= 2u;
                }
                char *tmp = (char *)realloc(line, next);
                if (!tmp) {
                    errno = ENOMEM;
                    goto fail;
                }
                line = tmp;
                cap = next;
            }
            line[len++] = ch;
        }
    }

    if (len > 0) {
        if (cut_fields_line(line, len, ranges, n, delim, suppress_no_delim, false) != 0) {
            goto fail;
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
    opterr = 0;

    const char *list = NULL;
    char delim = '\t';
    bool delim_set = false;
    bool suppress_no_delim = false;
    bool no_split = false;
    bool bflag = false;
    bool cflag = false;
    bool fflag = false;

    int opt;
    while ((opt = getopt(argc, argv, ":b:c:d:f:sn")) != -1) {
        switch (opt) {
        case 'b':
            list = optarg;
            bflag = true;
            break;
        case 'c':
            list = optarg;
            cflag = true;
            break;
        case 'f':
            list = optarg;
            fflag = true;
            break;
        case 'd':
            delim_set = true;
            if (!optarg || optarg[0] == '\0' || optarg[1] != '\0') {
                eprintf("cut: delimiter must be a single character\n");
                usage();
                return 1;
            }
            delim = optarg[0];
            break;
        case 's':
            suppress_no_delim = true;
            break;
        case 'n':
            no_split = true;
            break;
        case ':':
            eprintf("cut: option requires an argument -- %c\n", optopt);
            usage();
            return 1;
        default:
            eprintf("cut: illegal option -- %c\n", optopt);
            usage();
            return 1;
        }
    }

    int mode_count = (bflag ? 1 : 0) + (cflag ? 1 : 0) + (fflag ? 1 : 0);
    if (mode_count == 0) {
        usage();
        return 1;
    }
    if (mode_count > 1) {
        eprintf("cut: only one type of list may be specified\n");
        usage();
        return 1;
    }
    if (!fflag && delim_set) {
        eprintf("cut: -d is only valid with -f\n");
        usage();
        return 1;
    }
    if (!fflag && suppress_no_delim) {
        eprintf("cut: -s is only valid with -f\n");
        usage();
        return 1;
    }
    if (!bflag && no_split) {
        eprintf("cut: -n is only valid with -b\n");
        usage();
        return 1;
    }

    if (!list || *list == '\0') {
        eprintf("cut: invalid byte, character, or field list\n");
        return 1;
    }

    range_t *ranges = NULL;
    size_t nr = 0;
    if (parse_ranges(list, &ranges, &nr) != 0) {
        if (errno == ENOMEM || errno == EOVERFLOW) {
            eprintf("cut: %s\n", strerror(errno));
        } else {
            eprintf("cut: invalid byte, character, or field list\n");
        }
        free(ranges);
        return 1;
    }

    /* TODO: Implement multibyte-aware -c/-n once locale support is available. */

    int failed = 0;
    if (optind >= argc) {
        int rc = fflag ? cut_stream_fields(STDIN_FILENO, ranges, nr, delim, suppress_no_delim)
                       : cut_stream_bytes(STDIN_FILENO, ranges, nr);
        if (rc != 0) {
            eprintf("cut: %s\n", strerror(errno));
            free(ranges);
            return 1;
        }
        free(ranges);
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
        int rc = fflag ? cut_stream_fields(fd, ranges, nr, delim, suppress_no_delim)
                       : cut_stream_bytes(fd, ranges, nr);
        int saved_errno = errno;
        if (fd != STDIN_FILENO) {
            (void)close(fd);
        }
        if (rc != 0) {
            errno = saved_errno;
            eprintf("cut: %s: %s\n", path, strerror(errno));
            failed = 1;
        }
    }

    free(ranges);
    return failed ? 1 : 0;
}
