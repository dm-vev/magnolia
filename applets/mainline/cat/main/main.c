#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <stdarg.h>
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
            return -1;
        }
        off += (size_t)w;
    }
    return 0;
}

static int cat_fd(int fd, const char *name)
{
    char buf[512];
    while (1) {
        ssize_t r = read(fd, buf, sizeof(buf));
        if (r < 0) {
            eprintf("cat: %s: %s\n", name, strerror(errno));
            return 1;
        }
        if (r == 0) {
            return 0;
        }
        if (write_all(STDOUT_FILENO, buf, (size_t)r) != 0) {
            eprintf("cat: write: %s\n", strerror(errno));
            return 1;
        }
    }
}

static int cat_one(const char *path)
{
    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        eprintf("cat: %s: %s\n", path, strerror(errno));
        return 1;
    }
    int rc = cat_fd(fd, path);
    (void)close(fd);
    return rc;
}

int main(int argc, char **argv)
{
    int opt;
    while ((opt = getopt(argc, argv, "u")) != -1) {
        switch (opt) {
        case 'u':
            /* POSIX: ignored (unbuffered; stdout is unbuffered for write(2) anyway). */
            break;
        default:
            eprintf("usage: cat [-u] [file ...]\n");
            return 1;
        }
    }

    if (optind >= argc) {
        return cat_fd(STDIN_FILENO, "-");
    }

    int failed = 0;
    for (int i = optind; i < argc; ++i) {
        const char *path = argv[i];
        if (path == NULL) {
            continue;
        }
        if (strcmp(path, "-") == 0) {
            failed |= cat_fd(STDIN_FILENO, "-");
            continue;
        }
        failed |= cat_one(path);
    }
    return failed ? 1 : 0;
}
