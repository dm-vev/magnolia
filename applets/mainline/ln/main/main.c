#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <stdbool.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
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

static int copy_file(const char *src, const char *dst, bool force)
{
    if (force) {
        (void)unlink(dst);
    }
    int in = open(src, O_RDONLY);
    if (in < 0) {
        return -1;
    }
    int out = open(dst, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (out < 0) {
        (void)close(in);
        return -1;
    }
    char buf[1024];
    while (1) {
        ssize_t r = read(in, buf, sizeof(buf));
        if (r < 0) {
            (void)close(in);
            (void)close(out);
            return -1;
        }
        if (r == 0) {
            break;
        }
        if (write_all(out, buf, (size_t)r) != 0) {
            (void)close(in);
            (void)close(out);
            return -1;
        }
    }
    (void)close(in);
    (void)close(out);
    return 0;
}

static void print_help(void)
{
    printf("usage: ln [OPTION]... TARGET LINK_NAME\n");
    printf("  -f           remove existing destination files\n");
    printf("  -s           symbolic links (not supported yet)\n");
    printf("      --help   display this help and exit\n");
    printf("      --version output version information and exit\n");
    printf("note: hard links are not implemented yet; ln falls back to copying.\n");
}

static void print_version(void)
{
    printf("ln (%s)\n", g_version);
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

    bool force = false;
    bool symlink = false;
    int opt;
    while ((opt = getopt(argc, argv, "fs")) != -1) {
        switch (opt) {
        case 'f':
            force = true;
            break;
        case 's':
            symlink = true;
            break;
        default:
            eprintf("usage: ln [-f] TARGET LINK_NAME\n");
            return 1;
        }
    }

    if (symlink) {
        eprintf("ln: symbolic links are not supported\n");
        return 1;
    }

    if (argc - optind != 2) {
        eprintf("usage: ln [-f] TARGET LINK_NAME\n");
        return 1;
    }
    const char *src = argv[optind];
    const char *dst = argv[optind + 1];
    if (copy_file(src, dst, force) != 0) {
        eprintf("ln: %s -> %s: %s\n", src, dst, strerror(errno));
        return 1;
    }
    return 0;
}

