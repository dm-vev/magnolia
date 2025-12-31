#include <errno.h>
#include <limits.h>
#include <stdbool.h>
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

static int build_line(int argc, char **argv, char **out_line, size_t *out_len)
{
    if (argc <= 1) {
        *out_line = NULL;
        *out_len = 0;
        return 0;
    }

    size_t len = 1; /* trailing newline */
    for (int i = 1; i < argc; ++i) {
        if (i > 1) {
            if (SIZE_MAX - len < 1) {
                errno = EOVERFLOW;
                return -1;
            }
            len += 1;
        }
        if (argv[i]) {
            size_t arg_len = strlen(argv[i]);
            if (SIZE_MAX - len < arg_len) {
                errno = EOVERFLOW;
                return -1;
            }
            len += arg_len;
        }
    }

    /* Guard the NUL terminator allocation from size_t wraparound. */
    if (len > SIZE_MAX - 1) {
        errno = EOVERFLOW;
        return -1;
    }
    char *line = (char *)malloc(len + 1);
    if (!line) {
        errno = ENOMEM;
        return -1;
    }

    size_t off = 0;
    for (int i = 1; i < argc; ++i) {
        if (i > 1) {
            line[off++] = ' ';
        }
        if (argv[i]) {
            size_t arg_len = strlen(argv[i]);
            memcpy(line + off, argv[i], arg_len);
            off += arg_len;
        }
    }
    line[off++] = '\n';
    line[off] = '\0';

    *out_line = line;
    *out_len = off;
    return 0;
}

int main(int argc, char **argv)
{
    const char *default_line = "y\n";
    const char *line = default_line;
    size_t line_len = 2;
    char *owned_line = NULL;

    if (argc > 1) {
        size_t len = 0;
        if (build_line(argc, argv, &owned_line, &len) != 0) {
            eprintf("yes: %s\n", strerror(errno));
            return 1;
        }
        line = owned_line;
        line_len = len;
    }

    char block[4096];
    const char *out_buf = line;
    size_t out_len = line_len;

    if (line_len > 0 && line_len <= sizeof(block)) {
        size_t off = 0;
        while (off + line_len <= sizeof(block)) {
            memcpy(block + off, line, line_len);
            off += line_len;
        }
        if (off > 0) {
            out_buf = block;
            out_len = off;
        }
    }

    while (1) {
        if (write_all(STDOUT_FILENO, out_buf, out_len) != 0) {
            eprintf("yes: stdout: %s\n", strerror(errno));
            free(owned_line);
            return 1;
        }
    }

    return 0;
}
