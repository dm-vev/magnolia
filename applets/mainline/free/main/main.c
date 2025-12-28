#include <errno.h>
#include <stdbool.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "kernel/core/elf/m_elf_app_api.h"

static const char *g_version = "Magnolia coreutils 0.1";

static uint64_t u64_div(uint64_t n, uint64_t d)
{
    if (d == 0) {
        return 0;
    }
    uint64_t q = 0;
    for (int i = 63; i >= 0; --i) {
        if ((n >> i) >= d) {
            n -= d << i;
            q |= 1ULL << i;
        }
    }
    return q;
}

static void u64_divmod(uint64_t n, uint64_t d, uint64_t *q, uint64_t *r)
{
    if (d == 0) {
        if (q) {
            *q = 0;
        }
        if (r) {
            *r = n;
        }
        return;
    }
    uint64_t qv = 0;
    for (int i = 63; i >= 0; --i) {
        if ((n >> i) >= d) {
            n -= d << i;
            qv |= 1ULL << i;
        }
    }
    if (q) {
        *q = qv;
    }
    if (r) {
        *r = n;
    }
}

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
    printf("usage: free [OPTION]...\n");
    printf("Display memory usage (system heap + current job heap).\n\n");
    printf("  -b            show output in bytes\n");
    printf("  -k            show output in KiB\n");
    printf("  -m            show output in MiB\n");
    printf("  -g            show output in GiB\n");
    printf("  -h            human-readable (auto units)\n");
    printf("  -v            verbose (extra columns)\n");
    printf("      --help     display this help and exit\n");
    printf("      --version  output version information and exit\n");
}

static void print_version(void)
{
    printf("free (%s)\n", g_version);
}

typedef enum {
    UNIT_BYTES = 0,
    UNIT_KIB,
    UNIT_MIB,
    UNIT_GIB,
    UNIT_HUMAN,
} unit_t;

static uint64_t unit_divisor(unit_t unit)
{
    switch (unit) {
    case UNIT_BYTES:
        return 1;
    case UNIT_KIB:
        return 1024ULL;
    case UNIT_MIB:
        return 1024ULL * 1024ULL;
    case UNIT_GIB:
        return 1024ULL * 1024ULL * 1024ULL;
    case UNIT_HUMAN:
    default:
        return 1;
    }
}

static const char *unit_label(unit_t unit)
{
    switch (unit) {
    case UNIT_BYTES:
        return "B";
    case UNIT_KIB:
        return "KiB";
    case UNIT_MIB:
        return "MiB";
    case UNIT_GIB:
        return "GiB";
    case UNIT_HUMAN:
    default:
        return "";
    }
}

static void fmt_human(uint64_t bytes, char out[16])
{
    static const char *suffix[] = { "B", "KiB", "MiB", "GiB", "TiB" };
    uint64_t divisor = 1;
    int i = 0;
    while (i < 4 && divisor <= (UINT64_MAX / 1024) && bytes >= (divisor * 1024)) {
        divisor *= 1024;
        i++;
    }
    uint64_t whole = 0;
    uint64_t rem = 0;
    u64_divmod(bytes, divisor, &whole, &rem);
    if (i > 0 && whole < 10) {
        uint64_t frac = u64_div(rem * 10, divisor);
        (void)snprintf(out, 16, "%llu.%llu%s",
                       (unsigned long long)whole,
                       (unsigned long long)frac,
                       suffix[i]);
    } else {
        (void)snprintf(out, 16, "%llu%s",
                       (unsigned long long)whole,
                       suffix[i]);
    }
}

static void fmt_value(unit_t unit, uint64_t bytes, char out[16])
{
    if (unit == UNIT_HUMAN) {
        fmt_human(bytes, out);
        return;
    }
    uint64_t div = unit_divisor(unit);
    uint64_t v = div ? u64_div(bytes, div) : bytes;
    (void)snprintf(out, 16, "%llu", (unsigned long long)v);
}

static void print_row(unit_t unit,
                      bool verbose,
                      const char *label,
                      uint64_t total_bytes,
                      uint64_t used_bytes,
                      uint64_t free_bytes,
                      uint64_t min_free_bytes,
                      uint64_t largest_bytes,
                      uint64_t peak_bytes,
                      uint64_t regions)
{
    char total_s[16], used_s[16], free_s[16];
    char min_s[16], large_s[16], peak_s[16], region_s[32];

    fmt_value(unit, total_bytes, total_s);
    fmt_value(unit, used_bytes, used_s);
    fmt_value(unit, free_bytes, free_s);

    if (verbose) {
        if (min_free_bytes == (uint64_t)-1) {
            strcpy(min_s, "-");
        } else {
            fmt_value(unit, min_free_bytes, min_s);
        }

        if (largest_bytes == (uint64_t)-1) {
            strcpy(large_s, "-");
        } else {
            fmt_value(unit, largest_bytes, large_s);
        }

        if (peak_bytes == (uint64_t)-1) {
            strcpy(peak_s, "-");
        } else {
            fmt_value(unit, peak_bytes, peak_s);
        }

        if (regions == (uint64_t)-1) {
            strcpy(region_s, "-");
        } else {
            (void)snprintf(region_s, sizeof(region_s), "%llu", (unsigned long long)regions);
        }
    }

    if (!verbose) {
        printf("%-4s %12s %12s %12s\n", label, total_s, used_s, free_s);
        return;
    }
    printf("%-4s %12s %12s %12s %12s %12s %12s %8s\n",
           label,
           total_s,
           used_s,
           free_s,
           min_s,
           large_s,
           peak_s,
           region_s);
}

int main(int argc, char **argv)
{
    unit_t unit = UNIT_KIB;
    bool verbose = false;
    bool end_opts = false;

    for (int i = 1; i < argc; ++i) {
        const char *arg = argv[i];
        if (arg == NULL) {
            continue;
        }

        if (!end_opts && strcmp(arg, "--") == 0) {
            end_opts = true;
            continue;
        }

        if (!end_opts && strcmp(arg, "--help") == 0) {
            print_help();
            return 0;
        }
        if (!end_opts && strcmp(arg, "--version") == 0) {
            print_version();
            return 0;
        }

        if (!end_opts && arg[0] == '-' && arg[1] != '\0') {
            for (const char *opt = arg + 1; *opt != '\0'; ++opt) {
                switch (*opt) {
                case 'b':
                    unit = UNIT_BYTES;
                    break;
                case 'k':
                    unit = UNIT_KIB;
                    break;
                case 'm':
                    unit = UNIT_MIB;
                    break;
                case 'g':
                    unit = UNIT_GIB;
                    break;
                case 'h':
                    unit = UNIT_HUMAN;
                    break;
                case 'v':
                    verbose = true;
                    break;
                default:
                    eprintf("usage: free [-b|-k|-m|-g|-h] [-v]\n");
                    eprintf("try 'free --help'\n");
                    return 1;
                }
            }
            continue;
        }

        eprintf("free: unexpected operand: %s\n", arg);
        eprintf("try 'free --help'\n");
        return 1;
    }

    magnolia_meminfo_t info = {0};
    info.size = (uint32_t)sizeof(info);
    int rc = m_meminfo(&info);
    if (rc != 0) {
        int err = -rc;
        if (err <= 0) {
            err = EINVAL;
        }
        eprintf("free: m_meminfo: %s\n", strerror(err));
        return 1;
    }

    if (!verbose) {
        printf("             total         used         free");
        if (unit != UNIT_HUMAN) {
            printf(" (%s)", unit_label(unit));
        }
        printf("\n");
    } else {
        printf("             total         used         free      minfree      largest         peak  regions");
        if (unit != UNIT_HUMAN) {
            printf(" (%s)", unit_label(unit));
        }
        printf("\n");
    }

    uint64_t heap_total = (uint64_t)info.heap_total_bytes;
    uint64_t heap_free = (uint64_t)info.heap_free_bytes;
    uint64_t heap_used = heap_total >= heap_free ? (heap_total - heap_free) : 0;
    print_row(unit,
              verbose,
              "Mem:",
              heap_total,
              heap_used,
              heap_free,
              (uint64_t)info.heap_min_free_bytes,
              (uint64_t)info.heap_largest_free_block_bytes,
              (uint64_t)-1,
              (uint64_t)-1);

    if (info.job_capacity_bytes == 0 && info.job_used_bytes == 0 && info.job_peak_bytes == 0 &&
        info.job_region_count == 0) {
        if (!verbose) {
            printf("Job:          n/a\n");
        } else {
            printf("Job:          n/a\n");
        }
        return 0;
    }

    uint64_t job_cap = (uint64_t)info.job_capacity_bytes;
    uint64_t job_used = (uint64_t)info.job_used_bytes;
    uint64_t job_free = job_cap >= job_used ? (job_cap - job_used) : 0;
    print_row(unit,
              verbose,
              "Job:",
              job_cap,
              job_used,
              job_free,
              (uint64_t)-1,
              (uint64_t)-1,
              (uint64_t)info.job_peak_bytes,
              (uint64_t)info.job_region_count);

    if (info.job_limit_bytes > 0) {
        char limit_s[16];
        fmt_value(unit, (uint64_t)info.job_limit_bytes, limit_s);
        printf("JobLimit: %s", limit_s);
        if (unit != UNIT_HUMAN) {
            printf(" (%s)", unit_label(unit));
        }
        printf("\n");
    }
    return 0;
}
