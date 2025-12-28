#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <stdbool.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
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

static void print_help(void)
{
    printf("usage: tee [OPTION]... [FILE]...\n");
    printf("  -a           append to the given FILEs\n");
    printf("      --help   display this help and exit\n");
    printf("      --version output version information and exit\n");
}

static void print_version(void)
{
    printf("tee (%s)\n", g_version);
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

    bool append = false;
    int opt;
    while ((opt = getopt(argc, argv, "a")) != -1) {
        switch (opt) {
        case 'a':
            append = true;
            break;
        default:
            eprintf("usage: tee [-a] [FILE...]\n");
            return 1;
        }
    }

    int out_fds[16];
    int out_count = 0;
    for (int i = optind; i < argc && out_count < (int)(sizeof(out_fds) / sizeof(out_fds[0])); ++i) {
        const char *path = argv[i];
        if (!path) {
            continue;
        }
        int flags = O_WRONLY | O_CREAT;
        flags |= append ? O_APPEND : O_TRUNC;
        int fd = open(path, flags, 0666);
        if (fd < 0) {
            eprintf("tee: %s: %s\n", path, strerror(errno));
            return 1;
        }
        out_fds[out_count++] = fd;
    }

    char buf[512];
    int failed = 0;
    while (1) {
        ssize_t r = read(STDIN_FILENO, buf, sizeof(buf));
        if (r < 0) {
            eprintf("tee: read: %s\n", strerror(errno));
            failed = 1;
            break;
        }
        if (r == 0) {
            break;
        }
        if (write_all(STDOUT_FILENO, buf, (size_t)r) != 0) {
            eprintf("tee: write: %s\n", strerror(errno));
            failed = 1;
            break;
        }
        for (int i = 0; i < out_count; ++i) {
            if (write_all(out_fds[i], buf, (size_t)r) != 0) {
                eprintf("tee: write: %s\n", strerror(errno));
                failed = 1;
            }
        }
    }

    for (int i = 0; i < out_count; ++i) {
        (void)close(out_fds[i]);
    }
    return failed ? 1 : 0;
}

