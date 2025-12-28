#include <errno.h>
#include <getopt.h>
#include <stdbool.h>
#include <stdlib.h>
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

int main(int argc, char **argv)
{
    bool logical = true;

    int opt;
    while ((opt = getopt(argc, argv, "LP")) != -1) {
        switch (opt) {
        case 'L':
            logical = true;
            break;
        case 'P':
            logical = false;
            break;
        default:
            eprintf("usage: pwd [-L|-P]\n");
            return 1;
        }
    }
    if (optind < argc) {
        eprintf("pwd: too many arguments\n");
        return 1;
    }

    char buf[256];
    if (logical) {
        const char *pwd = getenv("PWD");
        if (pwd != NULL && pwd[0] == '/') {
            printf("%s\n", pwd);
            return 0;
        }
    }
    if (getcwd(buf, sizeof(buf)) == NULL) {
        eprintf("pwd: %s\n", strerror(errno));
        return 1;
    }
    printf("%s\n", buf);
    return 0;
}
