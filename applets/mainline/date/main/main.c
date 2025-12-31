#include <errno.h>
#include <getopt.h>
#include <limits.h>
#include <stdbool.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

/* BSD reference: FreeBSD date(1). */
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

static int write_line(const char *s, size_t len)
{
    if (len > 0 && write_all(STDOUT_FILENO, s, len) != 0) {
        return -1;
    }
    return write_all(STDOUT_FILENO, "\n", 1);
}

static bool streq(const char *a, const char *b)
{
    return a && b && strcmp(a, b) == 0;
}

static void print_help(void)
{
    printf("usage: date [OPTION]... [+FORMAT]\n");
    printf("  -u           print UTC time\n");
    printf("      --help   display this help and exit\n");
    printf("      --version output version information and exit\n");
}

static void print_version(void)
{
    printf("date (%s)\n", g_version);
}

static int format_time(const char *fmt, const struct tm *tm, char **out, size_t *out_len)
{
    size_t size = 128;
    /* Cap growth to avoid unbounded allocations on invalid format strings. */
    const size_t max_size = 1024 * 1024;
    if (fmt == NULL || tm == NULL || out == NULL || out_len == NULL) {
        errno = EINVAL;
        return -1;
    }
    if (fmt[0] == '\0') {
        *out = NULL;
        *out_len = 0;
        return 0;
    }

    for (;;) {
        char *buf = (char *)malloc(size);
        if (buf == NULL) {
            return -1;
        }
        size_t n = strftime(buf, size, fmt, tm);
        if (n > 0) {
            *out = buf;
            *out_len = n;
            return 0;
        }
        free(buf);

        if (size >= max_size || size > SIZE_MAX / 2 || size >= (size_t)INT_MAX) {
            errno = EOVERFLOW;
            return -1;
        }
        size *= 2;
    }
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

    bool utc = false;
    int opt;
    while ((opt = getopt(argc, argv, "u")) != -1) {
        switch (opt) {
        case 'u':
            utc = true;
            break;
        default:
            eprintf("usage: date [-u] [+FORMAT]\n");
            return 1;
        }
    }

    const char *fmt = "%Y-%m-%d %H:%M:%S";
    if (optind < argc && argv[optind] && argv[optind][0] == '+') {
        fmt = argv[optind] + 1;
        optind++;
    }
    if (optind < argc) {
        eprintf("date: extra operand: %s\n", argv[optind] ? argv[optind] : "");
        return 1;
    }

    time_t now = time(NULL);
    struct tm *tm = utc ? gmtime(&now) : localtime(&now);
    if (!tm) {
        eprintf("date: time conversion failed\n");
        return 1;
    }

    char *out = NULL;
    size_t out_len = 0;
    if (format_time(fmt, tm, &out, &out_len) != 0) {
        eprintf("date: invalid format\n");
        return 1;
    }

    int rc = write_line(out ? out : "", out_len);
    free(out);
    if (rc != 0) {
        eprintf("date: stdout: %s\n", strerror(errno));
        return 1;
    }
    return 0;
}
