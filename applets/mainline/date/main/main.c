#include <errno.h>
#include <getopt.h>
#include <stdbool.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
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
    printf("usage: date [OPTION]... [+FORMAT]\n");
    printf("  -u           print UTC time\n");
    printf("      --help   display this help and exit\n");
    printf("      --version output version information and exit\n");
}

static void print_version(void)
{
    printf("date (%s)\n", g_version);
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

    char out[128];
    size_t n = strftime(out, sizeof(out), fmt, tm);
    if (n == 0) {
        eprintf("date: invalid format\n");
        return 1;
    }
    printf("%s\n", out);
    return 0;
}
