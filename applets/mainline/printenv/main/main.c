#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

extern char **environ;

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

static int write_line(const char *s)
{
    size_t len = s ? strlen(s) : 0;
    if (len > 0 && write_all(STDOUT_FILENO, s, len) != 0) {
        return -1;
    }
    char nl = '\n';
    return write_all(STDOUT_FILENO, &nl, 1);
}

int main(int argc, char **argv)
{
    if (argc <= 1) {
        for (char **env = environ; env && *env; ++env) {
            if (write_line(*env) != 0) {
                eprintf("printenv: stdout: %s\n", strerror(errno));
                return 1;
            }
        }
        return 0;
    }

    int exit_status = 0;
    for (int i = 1; i < argc; ++i) {
        const char *value = getenv(argv[i]);
        if (value == NULL) {
            exit_status = 1;
            continue;
        }
        if (write_line(value) != 0) {
            eprintf("printenv: stdout: %s\n", strerror(errno));
            return 1;
        }
    }

    return exit_status;
}
