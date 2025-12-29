#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <limits.h>
#include <stdbool.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include <utime.h>

#ifndef ENOTSUP
#ifdef EOPNOTSUPP
#define ENOTSUP EOPNOTSUPP
#else
#define ENOTSUP EINVAL
#endif
#endif

typedef struct {
    bool no_create;
    bool update_atime;
    bool update_mtime;
    bool no_follow;
} touch_config_t;

typedef struct {
    time_t atime;
    time_t mtime;
} touch_base_t;

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

static void usage(void)
{
    eprintf("usage: touch [-A [-][[hh]mm]ss] [-acfmh] [-r file] "
            "[-t [[CC]YY]MMDDhhmm[.SS]] file ...\n");
}

static bool parse_digits(const char *s, size_t len, int *out)
{
    int value = 0;
    for (size_t i = 0; i < len; ++i) {
        char c = s[i];
        if (c < '0' || c > '9') {
            return false;
        }
        value = (value * 10) + (c - '0');
    }
    *out = value;
    return true;
}

static bool parse_two_digits(const char *s, int *out)
{
    return s != NULL && parse_digits(s, 2, out);
}

static bool is_leap_year(int year)
{
    if ((year % 4) != 0) {
        return false;
    }
    if ((year % 100) != 0) {
        return true;
    }
    return (year % 400) == 0;
}

static int days_in_month(int year, int month)
{
    static const int days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (month < 1 || month > 12) {
        return 0;
    }
    if (month == 2 && is_leap_year(year)) {
        return 29;
    }
    return days[month - 1];
}

static bool tm_matches(const struct tm *a, const struct tm *b)
{
    return a->tm_year == b->tm_year && a->tm_mon == b->tm_mon && a->tm_mday == b->tm_mday
           && a->tm_hour == b->tm_hour && a->tm_min == b->tm_min && a->tm_sec == b->tm_sec;
}

static bool current_year(int *out)
{
    time_t now = time(NULL);
    if (now == (time_t)-1) {
        return false;
    }
    struct tm *tm_now = localtime(&now);
    if (!tm_now) {
        return false;
    }
    *out = tm_now->tm_year + 1900;
    return true;
}

static bool parse_time_spec(const char *spec, time_t *out)
{
    if (!spec || *spec == '\0') {
        return false;
    }

    const char *dot = strchr(spec, '.');
    size_t main_len = dot ? (size_t)(dot - spec) : strlen(spec);
    int sec = 0;

    if (dot) {
        if (dot[1] == '\0' || dot[2] == '\0' || dot[3] != '\0') {
            return false;
        }
        if (!parse_two_digits(dot + 1, &sec)) {
            return false;
        }
    }

    if (main_len != 8 && main_len != 10 && main_len != 12) {
        return false;
    }
    for (size_t i = 0; i < main_len; ++i) {
        char c = spec[i];
        if (c < '0' || c > '9') {
            return false;
        }
    }

    int year = 0;
    size_t pos = 0;
    if (main_len == 12) {
        if (!parse_digits(spec, 4, &year)) {
            return false;
        }
        pos = 4;
    } else if (main_len == 10) {
        int yy = 0;
        if (!parse_digits(spec, 2, &yy)) {
            return false;
        }
        /* TODO: Confirm BSD touch pivot for 2-digit years (using POSIX 69/68). */
        year = (yy >= 69) ? (1900 + yy) : (2000 + yy);
        pos = 2;
    } else {
        if (!current_year(&year)) {
            return false;
        }
        pos = 0;
    }

    int month = 0;
    int day = 0;
    int hour = 0;
    int min = 0;
    if (!parse_two_digits(spec + pos, &month) || !parse_two_digits(spec + pos + 2, &day)
        || !parse_two_digits(spec + pos + 4, &hour) || !parse_two_digits(spec + pos + 6, &min)) {
        return false;
    }

    if (month < 1 || month > 12) {
        return false;
    }
    if (day < 1 || day > days_in_month(year, month)) {
        return false;
    }
    if (hour < 0 || hour > 23) {
        return false;
    }
    if (min < 0 || min > 59) {
        return false;
    }
    if (sec < 0 || sec > 59) {
        return false;
    }

    struct tm tm_value;
    memset(&tm_value, 0, sizeof(tm_value));
    tm_value.tm_year = year - 1900;
    tm_value.tm_mon = month - 1;
    tm_value.tm_mday = day;
    tm_value.tm_hour = hour;
    tm_value.tm_min = min;
    tm_value.tm_sec = sec;
    tm_value.tm_isdst = -1;

    time_t when = mktime(&tm_value);
    struct tm *round_trip = localtime(&when);
    if (!round_trip || !tm_matches(&tm_value, round_trip)) {
        return false;
    }

    *out = when;
    return true;
}

static bool add_time_checked(time_t base, int64_t delta, time_t *out)
{
    int64_t base64 = (int64_t)base;
    if (delta > 0 && base64 > INT64_MAX - delta) {
        return false;
    }
    if (delta < 0 && base64 < INT64_MIN - delta) {
        return false;
    }
    int64_t result = base64 + delta;
    if ((time_t)result != result) {
        return false;
    }
    *out = (time_t)result;
    return true;
}

static int parse_adjust(const char *arg, int64_t *out)
{
    if (!arg || *arg == '\0') {
        return -1;
    }

    bool negative = false;
    if (*arg == '+' || *arg == '-') {
        negative = (*arg == '-');
        arg++;
    }
    if (*arg == '\0') {
        return -1;
    }

    size_t len = strlen(arg);
    if (len == 0 || len > 6) {
        return -1;
    }

    int64_t value = 0;
    for (size_t i = 0; i < len; ++i) {
        char c = arg[i];
        if (c < '0' || c > '9') {
            return -1;
        }
        if (value > (INT64_MAX - 9) / 10) {
            return -1;
        }
        value = (value * 10) + (c - '0');
    }

    int64_t ss = 0;
    int64_t mm = 0;
    int64_t hh = 0;
    if (len <= 2) {
        ss = value;
    } else if (len <= 4) {
        ss = value % 100;
        mm = value / 100;
    } else {
        ss = value % 100;
        mm = (value / 100) % 100;
        hh = value / 10000;
    }

    if (ss > 59 || mm > 59) {
        return -1;
    }

    if (hh > INT64_MAX / 3600) {
        return -1;
    }
    int64_t total = hh * 3600;
    if (mm > (INT64_MAX - total) / 60) {
        return -1;
    }
    total += mm * 60;
    if (ss > INT64_MAX - total) {
        return -1;
    }
    total += ss;
    if (negative) {
        total = -total;
    }

    *out = total;
    return 0;
}

static int stat_path(const char *path, bool no_follow, struct stat *st)
{
    if (no_follow) {
        return lstat(path, st);
    }
    return stat(path, st);
}

static int touch_simple(const char *path, const touch_config_t *cfg, const touch_base_t *base)
{
    struct utimbuf times;
    times.actime = base->atime;
    times.modtime = base->mtime;

    if (utime(path, &times) == 0) {
        return 0;
    }
    if (errno != ENOENT) {
        return -1;
    }
    if (cfg->no_create) {
        return 0;
    }

    int fd = open(path, O_WRONLY | O_CREAT | O_EXCL, 0666);
    if (fd < 0) {
        if (errno != EEXIST) {
            return -1;
        }
    } else {
        (void)close(fd);
    }
    return utime(path, &times);
}

static int touch_with_stat(const char *path, const touch_config_t *cfg, const touch_base_t *base)
{
    struct stat st;
    if (stat_path(path, cfg->no_follow, &st) != 0) {
        if (errno != ENOENT) {
            return -1;
        }
        if (cfg->no_create) {
            return 0;
        }
        int fd = open(path, O_WRONLY | O_CREAT | O_EXCL, 0666);
        if (fd < 0) {
            if (errno != EEXIST) {
                return -1;
            }
        } else {
            (void)close(fd);
        }
        if (stat_path(path, cfg->no_follow, &st) != 0) {
            return -1;
        }
    }

    if (cfg->no_follow && S_ISLNK(st.st_mode)) {
        /* TODO: Update symlink timestamps when libc provides lutimes/utimensat. */
        errno = ENOTSUP;
        return -1;
    }

    /* TODO: Prefer utimensat/futimens with UTIME_OMIT to avoid stat/utime TOCTOU. */
    struct utimbuf times;
    times.actime = cfg->update_atime ? base->atime : st.st_atime;
    times.modtime = cfg->update_mtime ? base->mtime : st.st_mtime;
    return utime(path, &times);
}

int main(int argc, char **argv)
{
    bool opt_a = false;
    bool opt_m = false;
    bool opt_c = false;
    bool opt_h = false;
    bool ref_set = false;
    bool time_set = false;
    bool adjust_set = false;
    const char *ref_path = NULL;
    time_t time_value = 0;
    int64_t adjust_seconds = 0;

    opterr = 0;
    optind = 1;

    int opt;
    while ((opt = getopt(argc, argv, "A:acfmhr:t:")) != -1) {
        switch (opt) {
        case 'A':
            if (parse_adjust(optarg, &adjust_seconds) != 0) {
                eprintf("touch: illegal time offset -- %s\n", optarg ? optarg : "");
                return 1;
            }
            adjust_set = true;
            break;
        case 'a':
            opt_a = true;
            break;
        case 'c':
            opt_c = true;
            break;
        case 'f':
            break;
        case 'm':
            opt_m = true;
            break;
        case 'h':
            opt_h = true;
            break;
        case 'r':
            ref_set = true;
            ref_path = optarg;
            break;
        case 't':
            if (!parse_time_spec(optarg, &time_value)) {
                eprintf("touch: illegal time format -- %s\n", optarg ? optarg : "");
                return 1;
            }
            time_set = true;
            break;
        default:
            if (optopt == 'A' || optopt == 'r' || optopt == 't') {
                eprintf("touch: option requires an argument -- %c\n", optopt);
            } else {
                eprintf("touch: illegal option -- %c\n", optopt);
            }
            usage();
            return 1;
        }
    }

    if (ref_set && time_set) {
        eprintf("touch: -r and -t are mutually exclusive\n");
        usage();
        return 1;
    }

    if (optind >= argc) {
        usage();
        return 1;
    }

    touch_config_t cfg = {
        .no_create = opt_c,
        .update_atime = opt_a || (!opt_a && !opt_m),
        .update_mtime = opt_m || (!opt_a && !opt_m),
        .no_follow = opt_h,
    };

    touch_base_t base;
    if (ref_set) {
        struct stat ref_st;
        /* TODO: Verify whether BSD touch applies -h to the reference file in -r. */
        if (stat_path(ref_path, opt_h, &ref_st) != 0) {
            eprintf("touch: %s: %s\n", ref_path ? ref_path : "", strerror(errno));
            return 1;
        }
        base.atime = ref_st.st_atime;
        base.mtime = ref_st.st_mtime;
    } else if (time_set) {
        base.atime = time_value;
        base.mtime = time_value;
    } else {
        time_t now = time(NULL);
        if (now == (time_t)-1) {
            eprintf("touch: time unavailable\n");
            return 1;
        }
        base.atime = now;
        base.mtime = now;
    }

    if (adjust_set) {
        time_t adjusted = 0;
        if (!add_time_checked(base.atime, adjust_seconds, &adjusted)) {
            eprintf("touch: time adjustment out of range\n");
            return 1;
        }
        base.atime = adjusted;
        if (!add_time_checked(base.mtime, adjust_seconds, &adjusted)) {
            eprintf("touch: time adjustment out of range\n");
            return 1;
        }
        base.mtime = adjusted;
    }

    int failed = 0;
    bool need_stat = cfg.no_follow || !cfg.update_atime || !cfg.update_mtime;
    for (int i = optind; i < argc; ++i) {
        const char *path = argv[i];
        if (!path) {
            continue;
        }
        int rc = need_stat ? touch_with_stat(path, &cfg, &base) : touch_simple(path, &cfg, &base);
        if (rc != 0) {
            eprintf("touch: %s: %s\n", path, strerror(errno));
            failed = 1;
        }
    }
    return failed ? 1 : 0;
}
