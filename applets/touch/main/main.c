#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <stdbool.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static const char *g_version = "Magnolia coreutils 0.1";

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

static bool streq(const char *a, const char *b)
{
    return a && b && strcmp(a, b) == 0;
}

static void print_help(void)
{
    printf("usage: touch [OPTION]... FILE...\n");
    printf("  -c           do not create any files\n");
    printf("  -a, -m       accepted but not fully implemented (no timestamp update yet)\n");
    printf("      --help   display this help and exit\n");
    printf("      --version output version information and exit\n");
}

static void print_version(void)
{
    printf("touch (%s)\n", g_version);
}

static int touch_one(const char *path, bool no_create)
{
    struct stat st;
    if (stat(path, &st) == 0) {
        int fd = open(path, O_RDONLY);
        if (fd >= 0) {
            (void)close(fd);
            return 0;
        }
        return 0;
    }
    if (no_create) {
        return 0;
    }
    int fd = open(path, O_WRONLY | O_CREAT, 0666);
    if (fd < 0) {
        return -1;
    }
    (void)close(fd);
    return 0;
}

int main(int argc, char **argv)
{
    for (int i = 1; i < argc; ++i) {
        if (streq(argv[i], "--help")) {
            print_help();
            return 0;
        }
        if (streq(argv[i], "--version")) {
            print_version();
            return 0;
        }
    }

    bool no_create = false;
    int opt;
    while ((opt = getopt(argc, argv, "cam")) != -1) {
        switch (opt) {
        case 'c':
            no_create = true;
            break;
        case 'a':
        case 'm':
            break;
        default:
            eprintf("usage: touch [-c] FILE...\n");
            return 1;
        }
    }

    if (optind >= argc) {
        eprintf("touch: missing file operand\n");
        return 1;
    }

    int failed = 0;
    for (int i = optind; i < argc; ++i) {
        const char *path = argv[i];
        if (!path) {
            continue;
        }
        if (touch_one(path, no_create) != 0) {
            eprintf("touch: %s: %s\n", path, strerror(errno));
            failed = 1;
        }
    }
    return failed ? 1 : 0;
}

