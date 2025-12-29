#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

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

static int oprintf(const char *fmt, ...)
{
    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (n <= 0) {
        return 0;
    }
    size_t len = (size_t)n;
    if (len >= sizeof(buf)) {
        len = sizeof(buf) - 1;
    }
    return write_all(STDOUT_FILENO, buf, len);
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

static bool streq(const char *a, const char *b)
{
    return a && b && strcmp(a, b) == 0;
}

static int parse_skip(const char *s, uint64_t *out)
{
    if (s == NULL || *s == '\0') {
        return -1;
    }
    errno = 0;
    char *end = NULL;
    unsigned long long value = strtoull(s, &end, 10);
    if (errno != 0 || end == s || *end != '\0') {
        return -1;
    }
    *out = (uint64_t)value;
    return 0;
}

static int skip_fd(int fd, uint64_t skip)
{
    if (skip == 0) {
        return 0;
    }

    if (skip <= (uint64_t)INT64_MAX) {
        off_t offset = (off_t)skip;
        if (lseek(fd, offset, SEEK_CUR) >= 0) {
            return 0;
        }
        if (errno != ESPIPE && errno != EINVAL) {
            return -1;
        }
    }

    unsigned char buf[4096];
    uint64_t left = skip;
    while (left > 0) {
        size_t chunk = left < sizeof(buf) ? (size_t)left : sizeof(buf);
        ssize_t r = read_retry(fd, buf, chunk);
        if (r < 0) {
            return -1;
        }
        if (r == 0) {
            return 0;
        }
        left -= (uint64_t)r;
    }
    return 0;
}

static int drain_fd(int fd)
{
    unsigned char buf[4096];
    while (1) {
        ssize_t r = read_retry(fd, buf, sizeof(buf));
        if (r < 0) {
            return -1;
        }
        if (r == 0) {
            return 0;
        }
    }
}

static int compare_streams(int fd1,
                           const char *name1,
                           int fd2,
                           const char *name2,
                           bool list,
                           bool silent)
{
    unsigned char buf1[4096];
    unsigned char buf2[4096];
    size_t len1 = 0;
    size_t pos1 = 0;
    size_t len2 = 0;
    size_t pos2 = 0;
    uint64_t byte_pos = 1;
    uint64_t line_no = 1;
    bool need_line = !list && !silent;
    bool differ = false;

    while (1) {
        if (pos1 == len1) {
            ssize_t r = read_retry(fd1, buf1, sizeof(buf1));
            if (r < 0) {
                eprintf("cmp: %s: %s\n", name1, strerror(errno));
                return 2;
            }
            len1 = (size_t)r;
            pos1 = 0;
        }
        if (pos2 == len2) {
            ssize_t r = read_retry(fd2, buf2, sizeof(buf2));
            if (r < 0) {
                eprintf("cmp: %s: %s\n", name2, strerror(errno));
                return 2;
            }
            len2 = (size_t)r;
            pos2 = 0;
        }

        if (len1 == 0 && len2 == 0) {
            return differ ? 1 : 0;
        }
        if (len1 == 0 || len2 == 0) {
            if (!silent) {
                const char *eof_name = (len1 == 0) ? name1 : name2;
                if (oprintf("cmp: EOF on %s\n", eof_name) != 0) {
                    eprintf("cmp: stdout: %s\n", strerror(errno));
                    return 2;
                }
            }
            return 1;
        }

        size_t avail1 = len1 - pos1;
        size_t avail2 = len2 - pos2;
        size_t chunk = avail1 < avail2 ? avail1 : avail2;

        for (size_t i = 0; i < chunk; ++i) {
            unsigned char c1 = buf1[pos1 + i];
            unsigned char c2 = buf2[pos2 + i];
            if (c1 != c2) {
                differ = true;
                if (list) {
                    if (!silent) {
                        if (oprintf("%llu %o %o\n",
                                    (unsigned long long)byte_pos,
                                    (unsigned)c1,
                                    (unsigned)c2) != 0) {
                            eprintf("cmp: stdout: %s\n", strerror(errno));
                            return 2;
                        }
                    }
                } else {
                    if (!silent) {
                        if (oprintf("%s %s differ: byte %llu, line %llu\n",
                                    name1,
                                    name2,
                                    (unsigned long long)byte_pos,
                                    (unsigned long long)line_no) != 0) {
                            eprintf("cmp: stdout: %s\n", strerror(errno));
                            return 2;
                        }
                    }
                    return 1;
                }
            }
            if (need_line && c1 == '\n') {
                line_no++;
            }
            byte_pos++;
        }

        pos1 += chunk;
        pos2 += chunk;
    }
}

int main(int argc, char **argv)
{
    bool list = false;
    bool silent = false;

    int opt;
    while ((opt = getopt(argc, argv, "lst")) != -1) {
        switch (opt) {
        case 'l':
            list = true;
            break;
        case 's':
            silent = true;
            break;
        case 't':
            break;
        default:
            eprintf("usage: cmp [-l | -s | -t] file1 file2 [skip1 [skip2]]\n");
            return 2;
        }
    }

    if (list && silent) {
        eprintf("usage: cmp [-l | -s | -t] file1 file2 [skip1 [skip2]]\n");
        return 2;
    }

    int remaining = argc - optind;
    if (remaining < 2 || remaining > 4) {
        eprintf("usage: cmp [-l | -s | -t] file1 file2 [skip1 [skip2]]\n");
        return 2;
    }

    const char *name1 = argv[optind];
    const char *name2 = argv[optind + 1];

    uint64_t skip1 = 0;
    uint64_t skip2 = 0;
    if (remaining >= 3) {
        if (parse_skip(argv[optind + 2], &skip1) != 0) {
            eprintf("cmp: illegal offset -- %s\n", argv[optind + 2]);
            return 2;
        }
    }
    if (remaining == 4) {
        if (parse_skip(argv[optind + 3], &skip2) != 0) {
            eprintf("cmp: illegal offset -- %s\n", argv[optind + 3]);
            return 2;
        }
    }

    if (streq(name1, "-") && streq(name2, "-")) {
        if (skip1 != skip2) {
            eprintf("cmp: standard input offsets must match when both files are -\n");
            return 2;
        }
        if (skip_fd(STDIN_FILENO, skip1) != 0) {
            eprintf("cmp: -: %s\n", strerror(errno));
            return 2;
        }
        if (drain_fd(STDIN_FILENO) != 0) {
            eprintf("cmp: -: %s\n", strerror(errno));
            return 2;
        }
        return 0;
    }

    int fd1 = -1;
    int fd2 = -1;
    bool close1 = false;
    bool close2 = false;

    if (streq(name1, "-")) {
        fd1 = STDIN_FILENO;
    } else {
        fd1 = open(name1, O_RDONLY);
        if (fd1 < 0) {
            eprintf("cmp: %s: %s\n", name1, strerror(errno));
            return 2;
        }
        close1 = true;
    }

    if (streq(name2, "-")) {
        fd2 = STDIN_FILENO;
    } else {
        fd2 = open(name2, O_RDONLY);
        if (fd2 < 0) {
            eprintf("cmp: %s: %s\n", name2, strerror(errno));
            if (close1) {
                (void)close(fd1);
            }
            return 2;
        }
        close2 = true;
    }

    if (skip_fd(fd1, skip1) != 0) {
        eprintf("cmp: %s: %s\n", name1, strerror(errno));
        if (close1) {
            (void)close(fd1);
        }
        if (close2) {
            (void)close(fd2);
        }
        return 2;
    }

    if (skip_fd(fd2, skip2) != 0) {
        eprintf("cmp: %s: %s\n", name2, strerror(errno));
        if (close1) {
            (void)close(fd1);
        }
        if (close2) {
            (void)close(fd2);
        }
        return 2;
    }

    int rc = compare_streams(fd1, name1, fd2, name2, list, silent);

    if (close1) {
        (void)close(fd1);
    }
    if (close2) {
        (void)close(fd2);
    }

    return rc;
}
