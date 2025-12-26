#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#include <errno.h>
#include <math.h>
#include <stdbool.h>
#include <stdarg.h>
#include <stdint.h>
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
    printf("usage: sleep NUMBER[SUFFIX]...\n");
    printf("Pause for the time specified by the sum of the arguments.\n\n");
    printf("SUFFIX may be 's' for seconds (default), 'm' for minutes,\n");
    printf("'h' for hours, or 'd' for days.\n\n");
    printf("      --help     display this help and exit\n");
    printf("      --version  output version information and exit\n");
}

static void print_version(void)
{
    printf("sleep (%s)\n", g_version);
}

static bool parse_duration_ns(const char *s, uint64_t *out_ns)
{
    if (s == NULL || out_ns == NULL || s[0] == '\0') {
        return false;
    }

    errno = 0;
    char *end = NULL;
    double value = strtod(s, &end);
    if (end == s || errno != 0 || !isfinite(value) || value < 0.0) {
        return false;
    }

    double mult = 1.0;
    if (*end != '\0') {
        if (end[1] != '\0') {
            return false;
        }
        switch ((unsigned char)end[0]) {
        case 's':
            mult = 1.0;
            break;
        case 'm':
            mult = 60.0;
            break;
        case 'h':
            mult = 60.0 * 60.0;
            break;
        case 'd':
            mult = 60.0 * 60.0 * 24.0;
            break;
        default:
            return false;
        }
    }

    double ns = value * mult * 1000000000.0;
    if (!isfinite(ns) || ns < 0.0 || ns > (double)UINT64_MAX) {
        return false;
    }

    uint64_t ns_int = (uint64_t)ceil(ns - 1e-9);
    if (value > 0.0 && ns_int == 0) {
        ns_int = 1;
    }
    *out_ns = ns_int;
    return true;
}

static int sleep_ns(uint64_t total_ns)
{
    uint64_t sec_u = total_ns / 1000000000ULL;
    time_t sec = (time_t)sec_u;
    if (sec < 0 || (uint64_t)sec != sec_u) {
        errno = EINVAL;
        return -1;
    }

    struct timespec req;
    req.tv_sec = sec;
    req.tv_nsec = (long)(total_ns % 1000000000ULL);

    while (req.tv_sec != 0 || req.tv_nsec != 0) {
        struct timespec rem = { 0 };
        if (nanosleep(&req, &rem) == 0) {
            return 0;
        }
        if (errno != EINTR) {
            return -1;
        }
        req = rem;
    }
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

    int i = 1;
    if (i < argc && argv[i] && streq(argv[i], "--")) {
        i++;
    }

    if (i >= argc) {
        eprintf("sleep: missing operand\n");
        eprintf("Try 'sleep --help' for more information.\n");
        return 1;
    }

    uint64_t total_ns = 0;
    for (; i < argc; ++i) {
        const char *arg = argv[i] ? argv[i] : "";
        uint64_t ns = 0;
        if (!parse_duration_ns(arg, &ns) || UINT64_MAX - total_ns < ns) {
            eprintf("sleep: invalid time interval '%s'\n", arg);
            return 1;
        }
        total_ns += ns;
    }

    if (sleep_ns(total_ns) != 0) {
        eprintf("sleep: %s\n", strerror(errno));
        return 1;
    }
    return 0;
}

