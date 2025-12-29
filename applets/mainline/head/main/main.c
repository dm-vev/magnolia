#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

enum header_mode {
    HEADER_AUTO = 0,
    HEADER_QUIET,
    HEADER_VERBOSE,
};

enum io_error {
    IO_OK = 0,
    IO_READ,
    IO_WRITE,
};

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

static int parse_count(const char *s, size_t *out)
{
    if (!s || *s == '\0') {
        return -1;
    }
    if (*s == '+' || *s == '-') {
        return -1;
    }
    errno = 0;
    char *end = NULL;
    unsigned long long v = strtoull(s, &end, 10);
    if (errno == ERANGE || end == s || *end != '\0') {
        return -1;
    }
    if (v > SIZE_MAX) {
        return -1;
    }
    *out = (size_t)v;
    return 0;
}

static int copy_n_bytes(int fd, size_t limit, enum io_error *err)
{
    char buf[512];
    size_t remaining = limit;

    while (remaining > 0) {
        size_t want = remaining < sizeof(buf) ? remaining : sizeof(buf);
        ssize_t r = read_retry(fd, buf, want);
        if (r < 0) {
            *err = IO_READ;
            return -1;
        }
        if (r == 0) {
            return 0;
        }
        if (write_all(STDOUT_FILENO, buf, (size_t)r) != 0) {
            *err = IO_WRITE;
            return -1;
        }
        remaining -= (size_t)r;
    }
    return 0;
}

static int copy_n_lines(int fd, size_t limit, enum io_error *err)
{
    char buf[512];
    size_t lines = 0;

    while (lines < limit) {
        ssize_t r = read_retry(fd, buf, sizeof(buf));
        if (r < 0) {
            *err = IO_READ;
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
                *err = IO_WRITE;
                return -1;
            }
        }
        if (lines >= limit) {
            return 0;
        }
    }
    return 0;
}

static int head_fd(int fd, bool by_bytes, size_t limit, enum io_error *err)
{
    *err = IO_OK;
    if (limit == 0) {
        return 0;
    }
    return by_bytes ? copy_n_bytes(fd, limit, err) : copy_n_lines(fd, limit, err);
}

static int print_header(const char *path, bool first)
{
    if (!first) {
        if (write_all(STDOUT_FILENO, "\n", 1) != 0) {
            return -1;
        }
    }
    if (write_all(STDOUT_FILENO, "==> ", 4) != 0) {
        return -1;
    }
    if (write_all(STDOUT_FILENO, path, strlen(path)) != 0) {
        return -1;
    }
    if (write_all(STDOUT_FILENO, " <==\n", 5) != 0) {
        return -1;
    }
    return 0;
}

static void usage(void)
{
    eprintf("usage: head [-n lines | -c bytes] [-qv] [file ...]\n");
}

int main(int argc, char **argv)
{
    size_t lines = 10;
    size_t bytes = 0;
    bool by_bytes = false;
    enum header_mode header_mode = HEADER_AUTO;

    int file_index = argc;
    for (int i = 1; i < argc; ++i) {
        const char *arg = argv[i];
        if (!arg) {
            continue;
        }
        if (strcmp(arg, "--") == 0) {
            file_index = i + 1;
            break;
        }
        if (arg[0] != '-' || arg[1] == '\0') {
            file_index = i;
            break;
        }
        if (isdigit((unsigned char)arg[1])) {
            /* BSD legacy: -N is treated as "first N lines". */
            if (parse_count(arg + 1, &lines) != 0) {
                eprintf("head: illegal line count -- %s\n", arg + 1);
                usage();
                return 1;
            }
            by_bytes = false;
            continue;
        }

        bool done = false;
        for (size_t j = 1; arg[j] != '\0'; ++j) {
            char ch = arg[j];
            switch (ch) {
            case 'n':
            case 'c': {
                const char *value = NULL;
                if (arg[j + 1] != '\0') {
                    value = arg + j + 1;
                } else {
                    if (i + 1 >= argc) {
                        eprintf("head: option requires an argument -- %c\n", ch);
                        usage();
                        return 1;
                    }
                    value = argv[++i];
                }
                size_t count = 0;
                if (parse_count(value, &count) != 0) {
                    if (ch == 'n') {
                        eprintf("head: illegal line count -- %s\n", value);
                    } else {
                        eprintf("head: illegal byte count -- %s\n", value);
                    }
                    usage();
                    return 1;
                }
                if (ch == 'n') {
                    lines = count;
                    by_bytes = false;
                } else {
                    bytes = count;
                    by_bytes = true;
                }
                done = true;
                break;
            }
            case 'q':
                header_mode = HEADER_QUIET;
                break;
            case 'v':
                header_mode = HEADER_VERBOSE;
                break;
            default:
                eprintf("head: illegal option -- %c\n", ch);
                usage();
                return 1;
            }
            if (done) {
                break;
            }
        }
    }

    size_t limit = by_bytes ? bytes : lines;
    if (file_index >= argc) {
        enum io_error err = IO_OK;
        if (head_fd(STDIN_FILENO, by_bytes, limit, &err) != 0) {
            if (err == IO_WRITE) {
                eprintf("head: stdout: %s\n", strerror(errno));
            } else {
                eprintf("head: -: %s\n", strerror(errno));
            }
            return 1;
        }
        return 0;
    }

    int file_count = argc - file_index;
    bool need_separator = false;
    int failed = 0;
    for (int i = file_index; i < argc; ++i) {
        const char *path = argv[i];
        if (!path) {
            continue;
        }

        bool show_header = false;
        if (header_mode == HEADER_VERBOSE) {
            show_header = true;
        } else if (header_mode == HEADER_AUTO && file_count > 1) {
            show_header = true;
        }

        if (show_header) {
            if (print_header(path, !need_separator) != 0) {
                eprintf("head: stdout: %s\n", strerror(errno));
                return 1;
            }
            need_separator = true;
        }

        int fd = STDIN_FILENO;
        if (strcmp(path, "-") != 0) {
            fd = open(path, O_RDONLY);
            if (fd < 0) {
                eprintf("head: %s: %s\n", path, strerror(errno));
                failed = 1;
                continue;
            }
        }

        enum io_error err = IO_OK;
        if (head_fd(fd, by_bytes, limit, &err) != 0) {
            if (err == IO_WRITE) {
                eprintf("head: stdout: %s\n", strerror(errno));
                if (fd != STDIN_FILENO) {
                    (void)close(fd);
                }
                return 1;
            }
            eprintf("head: %s: %s\n", path, strerror(errno));
            failed = 1;
        }
        if (fd != STDIN_FILENO) {
            (void)close(fd);
        }
    }
    return failed ? 1 : 0;
}
