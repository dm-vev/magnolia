#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

struct line_buf {
    unsigned char *data;
    size_t len;
    size_t cap;
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

static int line_buf_reserve(struct line_buf *line, size_t needed)
{
    if (line->cap >= needed) {
        return 0;
    }
    size_t new_cap = line->cap == 0 ? 256 : line->cap;
    while (new_cap < needed) {
        if (new_cap > (SIZE_MAX / 2)) {
            errno = ENOMEM;
            return -1;
        }
        new_cap *= 2;
    }
    unsigned char *next = realloc(line->data, new_cap);
    if (next == NULL) {
        return -1;
    }
    line->data = next;
    line->cap = new_cap;
    return 0;
}

static int line_buf_push(struct line_buf *line, unsigned char c)
{
    if (line->len == SIZE_MAX) {
        errno = ENOMEM;
        return -1;
    }
    size_t needed = line->len + 1;
    if (needed > line->cap) {
        if (line_buf_reserve(line, needed) != 0) {
            return -1;
        }
    }
    line->data[line->len++] = c;
    return 0;
}

static void reverse_line(struct line_buf *line)
{
    size_t i = 0;
    size_t j = line->len;
    if (j == 0) {
        return;
    }
    j--;
    while (i < j) {
        unsigned char tmp = line->data[i];
        line->data[i] = line->data[j];
        line->data[j] = tmp;
        ++i;
        --j;
    }
}

static int emit_line(struct line_buf *line, bool with_newline)
{
    if (line->len > 0) {
        reverse_line(line);
        if (write_all(STDOUT_FILENO, line->data, line->len) != 0) {
            return -1;
        }
    }
    if (with_newline) {
        char nl = '\n';
        if (write_all(STDOUT_FILENO, &nl, 1) != 0) {
            return -1;
        }
    }
    line->len = 0;
    return 0;
}

static int rev_fd(int fd, const char *name)
{
    unsigned char buf[4096];
    struct line_buf line = { 0 };
    int status = 0;

    while (1) {
        ssize_t r = read_retry(fd, buf, sizeof(buf));
        if (r < 0) {
            eprintf("rev: %s: %s\n", name, strerror(errno));
            status = 1;
            break;
        }
        if (r == 0) {
            break;
        }
        for (ssize_t i = 0; i < r; ++i) {
            unsigned char c = buf[i];
            if (c == '\n') {
                if (emit_line(&line, true) != 0) {
                    eprintf("rev: stdout: %s\n", strerror(errno));
                    status = 1;
                    goto out;
                }
                continue;
            }
            if (line_buf_push(&line, c) != 0) {
                eprintf("rev: out of memory\n");
                status = 1;
                goto out;
            }
        }
    }

    if (status == 0 && line.len > 0) {
        if (emit_line(&line, false) != 0) {
            eprintf("rev: stdout: %s\n", strerror(errno));
            status = 1;
        }
    }

out:
    free(line.data);
    return status;
}

int main(int argc, char **argv)
{
    int status = 0;

    if (argc <= 1) {
        return rev_fd(STDIN_FILENO, "stdin");
    }

    for (int i = 1; i < argc; ++i) {
        const char *path = argv[i];
        if (strcmp(path, "-") == 0) {
            if (rev_fd(STDIN_FILENO, path) != 0) {
                status = 1;
            }
            continue;
        }
        int fd = open(path, O_RDONLY);
        if (fd < 0) {
            eprintf("rev: %s: %s\n", path, strerror(errno));
            status = 1;
            continue;
        }
        if (rev_fd(fd, path) != 0) {
            status = 1;
        }
        close(fd);
    }

    return status;
}
