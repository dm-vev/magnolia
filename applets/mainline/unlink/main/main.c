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

static void usage(void)
{
    eprintf("usage: unlink file\n");
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

    if (unlink(path) != 0) {
        eprintf("unlink: %s: %s\n", path, strerror(errno));
        return 1;
    }
    return 0;
}
