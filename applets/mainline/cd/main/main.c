#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdarg.h>
#include <stdbool.h>

/*
 * BSD reference: FreeBSD sh(1) built-in cd
 *
 * Behavior notes:
 * - With no argument or an empty string, use $HOME and fall back to "/".
 * - "cd -" changes to $OLDPWD and prints the new directory on success.
 */

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

int main(int argc, char **argv)
{
    const char *path = NULL;
    bool print_path = false;

    if (argc > 2) {
        eprintf("cd: too many arguments\n");
        return 1;
    }

    if (argc < 2 || argv[1] == NULL || argv[1][0] == '\0') {
        path = getenv("HOME");
        if (path == NULL || path[0] == '\0') {
            path = "/";
        }
    } else if (strcmp(argv[1], "-") == 0) {
        path = getenv("OLDPWD");
        if (path == NULL || path[0] == '\0') {
            eprintf("cd: OLDPWD not set\n");
            return 1;
        }
        print_path = true;
    } else {
        path = argv[1];
    }

    if (chdir(path) != 0) {
        eprintf("cd: %s: %s\n", path, strerror(errno));
        return 1;
    }

    if (print_path) {
        size_t len = strlen(path);
        if ((len > 0 && write_all(STDOUT_FILENO, path, len) != 0) ||
            write_all(STDOUT_FILENO, "\n", 1) != 0) {
            eprintf("cd: stdout: %s\n", strerror(errno));
            return 1;
        }
    }
    return 0;
}
