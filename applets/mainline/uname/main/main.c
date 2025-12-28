#include <errno.h>
#include <getopt.h>
#include <stdbool.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "sdkconfig.h"

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

typedef struct {
    bool sysname;
    bool nodename;
    bool release;
    bool version;
    bool machine;
    bool processor;
    bool hw_platform;
    bool operating_system;
} uname_opts_t;

static const char *uname_sysname(void)
{
    return "Magnolia";
}

static const char *uname_nodename(void)
{
    const char *v = getenv("HOSTNAME");
    if (v && v[0] != '\0') {
        return v;
    }
    v = getenv("HOST");
    if (v && v[0] != '\0') {
        return v;
    }
    return "magnolia";
}

static const char *uname_release(void)
{
    return "0.1";
}

static const char *uname_version(void)
{
    return __DATE__ " " __TIME__;
}

static const char *uname_machine(void)
{
#if defined(CONFIG_IDF_TARGET)
    return CONFIG_IDF_TARGET;
#elif defined(__XTENSA__)
    return "xtensa";
#elif defined(__riscv) || defined(__riscv__)
    return "riscv";
#else
    return "unknown";
#endif
}

static const char *uname_processor(void)
{
#if defined(CONFIG_IDF_TARGET_ARCH)
    return CONFIG_IDF_TARGET_ARCH;
#elif defined(__XTENSA__)
    return "xtensa";
#elif defined(__riscv) || defined(__riscv__)
    return "riscv";
#else
    return "unknown";
#endif
}

static const char *uname_hw_platform(void)
{
    return uname_machine();
}

static const char *uname_operating_system(void)
{
    return "Magnolia";
}

static void print_help(void)
{
    printf("usage: uname [OPTION]...\n");
    printf("  -a  print all information\n");
    printf("  -s  print the kernel name\n");
    printf("  -n  print the network node hostname\n");
    printf("  -r  print the kernel release\n");
    printf("  -v  print the kernel version\n");
    printf("  -m  print the machine hardware name\n");
    printf("  -p  print the processor type\n");
    printf("  -i  print the hardware platform\n");
    printf("  -o  print the operating system\n");
    printf("      --help     display this help and exit\n");
    printf("      --version  output version information and exit\n");
}

static void print_version(void)
{
    printf("uname (%s)\n", g_version);
}

static void select_all(uname_opts_t *opts)
{
    opts->sysname = true;
    opts->nodename = true;
    opts->release = true;
    opts->version = true;
    opts->machine = true;
    opts->processor = true;
    opts->hw_platform = true;
    opts->operating_system = true;
}

static bool any_selected(const uname_opts_t *opts)
{
    return opts->sysname || opts->nodename || opts->release || opts->version || opts->machine
           || opts->processor || opts->hw_platform || opts->operating_system;
}

static int uname_print(const uname_opts_t *opts)
{
    const char *fields[8];
    int n = 0;

    if (opts->sysname) {
        fields[n++] = uname_sysname();
    }
    if (opts->nodename) {
        fields[n++] = uname_nodename();
    }
    if (opts->release) {
        fields[n++] = uname_release();
    }
    if (opts->version) {
        fields[n++] = uname_version();
    }
    if (opts->machine) {
        fields[n++] = uname_machine();
    }
    if (opts->processor) {
        fields[n++] = uname_processor();
    }
    if (opts->hw_platform) {
        fields[n++] = uname_hw_platform();
    }
    if (opts->operating_system) {
        fields[n++] = uname_operating_system();
    }

    for (int i = 0; i < n; ++i) {
        if (i != 0) {
            putchar(' ');
        }
        fputs(fields[i] ? fields[i] : "", stdout);
    }
    putchar('\n');
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

    uname_opts_t opts = {0};

    int opt;
    while ((opt = getopt(argc, argv, "asnrvmpio")) != -1) {
        switch (opt) {
        case 'a':
            select_all(&opts);
            break;
        case 's':
            opts.sysname = true;
            break;
        case 'n':
            opts.nodename = true;
            break;
        case 'r':
            opts.release = true;
            break;
        case 'v':
            opts.version = true;
            break;
        case 'm':
            opts.machine = true;
            break;
        case 'p':
            opts.processor = true;
            break;
        case 'i':
            opts.hw_platform = true;
            break;
        case 'o':
            opts.operating_system = true;
            break;
        default:
            eprintf("usage: uname [-asnrvmpio] [-a]\n");
            return 1;
        }
    }

    if (optind < argc) {
        eprintf("uname: extra operand: %s\n", argv[optind] ? argv[optind] : "");
        return 1;
    }

    if (!any_selected(&opts)) {
        opts.sysname = true;
    }

    return uname_print(&opts);
}
