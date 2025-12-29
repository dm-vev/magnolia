#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#include <errno.h>
#include <limits.h>
#include <math.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
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

// Avoid undefined conversion when turning parsed seconds into time_t.
static time_t time_t_max(void)
{
    if ((time_t)-1 > 0) {
        return (time_t)~(time_t)0;
    }
    size_t bits = sizeof(time_t) * CHAR_BIT;
    if (bits >= sizeof(uintmax_t) * CHAR_BIT) {
        return (time_t)INTMAX_MAX;
    }
    uintmax_t max = ((uintmax_t)1 << (bits - 1)) - 1;
    return (time_t)max;
}

static void usage(void)
{
    eprintf("usage: sleep seconds\n");
}

static void illegal_interval(const char *arg)
{
    eprintf("sleep: illegal time interval -- %s\n", arg ? arg : "");
    usage();
}

static int parse_seconds(const char *arg, struct timespec *out)
{
    if (arg == NULL || out == NULL) {
        return -1;
    }

    errno = 0;
    char *end = NULL;
    double value = strtod(arg, &end);
    if (end == arg || *end != '\0' || errno != 0 || !isfinite(value) || value < 0.0) {
        return -1;
    }

    double intpart = 0.0;
    double frac = modf(value, &intpart);
    if (frac < 0.0) {
        return -1;
    }

    time_t max_sec = time_t_max();
    double max_sec_d = (double)max_sec;
    if (value > max_sec_d) {
        return -1;
    }
    time_t sec = (time_t)intpart;
    if ((double)sec != intpart) {
        return -1;
    }

    long nsec = (long)(frac * 1000000000.0);
    if (nsec < 0) {
        nsec = 0;
    }
    if (nsec >= 1000000000L) {
        if (sec == max_sec) {
            return -1;
        }
        sec++;
        nsec -= 1000000000L;
    }

    out->tv_sec = sec;
    out->tv_nsec = nsec;
    return 0;
}

static int sleep_for(const struct timespec *initial)
{
    struct timespec req = *initial;
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
    if (argc < 2) {
        usage();
        return 1;
    }

    struct timespec req = { 0 };
    if (parse_seconds(argv[1], &req) != 0) {
        illegal_interval(argv[1]);
        return 1;
    }
    if (argc > 2) {
        illegal_interval(argv[2]);
        return 1;
    }

    if (sleep_for(&req) != 0) {
        eprintf("sleep: %s\n", strerror(errno));
        return 1;
    }
    return 0;
}
