#include <errno.h>
#include <getopt.h>
#include <stdbool.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "sdkconfig.h"

/*
 * BSD reference: FreeBSD uname(1).
 *
 * Behavior notes:
 * - Default output is the system name only.
 * - -a is equivalent to -s -n -r -v -m on FreeBSD.
 */

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

typedef struct {
    bool sysname;
    bool nodename;
    bool release;
    bool version;
    bool machine;
    bool processor;
} uname_opts_t;

static const char *uname_sysname(void)
{
    return "Magnolia";
}

static const char *uname_nodename(void)
{
    /* Fall back to environment hints for minimal environments without hostname APIs. */
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
    return "unknown";
}

static const char *uname_arch(void)
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

static const char *uname_machine(void)
{
    return uname_arch();
}

static const char *uname_processor(void)
{
    return uname_arch();
}

static void select_all(uname_opts_t *opts)
{
    opts->sysname = true;
    opts->nodename = true;
    opts->release = true;
    opts->version = true;
    opts->machine = true;
}

static bool any_selected(const uname_opts_t *opts)
{
    return opts->sysname || opts->nodename || opts->release || opts->version || opts->machine
           || opts->processor;
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

    for (int i = 0; i < n; ++i) {
        if (i != 0) {
            if (write_all(STDOUT_FILENO, " ", 1) != 0) {
                return -1;
            }
        }
        if (fields[i] != NULL) {
            size_t len = strlen(fields[i]);
            if (len > 0 && write_all(STDOUT_FILENO, fields[i], len) != 0) {
                return -1;
            }
        }
    }
    if (write_all(STDOUT_FILENO, "\n", 1) != 0) {
        return -1;
    }
    return 0;
}

int main(int argc, char **argv)
{
    uname_opts_t opts = {0};

    int opt;
    opterr = 0;
    while ((opt = getopt(argc, argv, "amnprsv")) != -1) {
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
        default:
            eprintf("uname: illegal option -- %c\n", optopt);
            eprintf("usage: uname [-amnprsv]\n");
            return 1;
        }
    }

    if (optind < argc) {
        eprintf("uname: extra operand: %s\n", argv[optind] ? argv[optind] : "");
        eprintf("usage: uname [-amnprsv]\n");
        return 1;
    }

    if (!any_selected(&opts)) {
        opts.sysname = true;
    }

    if (uname_print(&opts) != 0) {
        eprintf("uname: stdout: %s\n", strerror(errno));
        return 1;
    }
    return 0;
}
