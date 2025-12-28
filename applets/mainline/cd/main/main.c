#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdarg.h>

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

int main(int argc, char **argv)
{
    const char *path = NULL;

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
        printf("%s\n", path);
    } else {
        path = argv[1];
    }

    if (chdir(path) != 0) {
        eprintf("cd: %s: %s\n", path, strerror(errno));
        return 1;
    }
    return 0;
}
