#include <errno.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
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

static void usage(void)
{
    eprintf("usage: dirname string\n");
}

static void compute_dirname(const char *path, const char **out_base, size_t *out_len)
{
    size_t len = path ? strlen(path) : 0;
    if (len == 0) {
        *out_base = ".";
        *out_len = 1;
        return;
    }

    size_t end = len;
    /* Strip trailing slashes so "/usr/" behaves like "/usr". */
    while (end > 0 && path[end - 1] == '/') {
        end--;
    }
    if (end == 0) {
        *out_base = "/";
        *out_len = 1;
        return;
    }

    size_t i = end;
    while (i > 0 && path[i - 1] != '/') {
        i--;
    }
    if (i == 0) {
        *out_base = ".";
        *out_len = 1;
        return;
    }

    /* Trim trailing slashes from the parent directory, preserving root. */
    size_t dir_len = i - 1;
    while (dir_len > 1 && path[dir_len - 1] == '/') {
        dir_len--;
    }
    if (dir_len == 0) {
        *out_base = "/";
        *out_len = 1;
        return;
    }

    *out_base = path;
    *out_len = dir_len;
}

int main(int argc, char **argv)
{
    const char *path = NULL;
    if (argc == 2) {
        path = argv[1];
    } else if (argc == 3 && strcmp(argv[1], "--") == 0) {
        path = argv[2];
    } else {
        usage();
        return 1;
    }

    const char *base = NULL;
    size_t base_len = 0;
    compute_dirname(path, &base, &base_len);
    if (write_all(STDOUT_FILENO, base, base_len) != 0 ||
        write_all(STDOUT_FILENO, "\n", 1) != 0) {
        eprintf("dirname: stdout: %s\n", strerror(errno));
        return 1;
    }
    return 0;
}
