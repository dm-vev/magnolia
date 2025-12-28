#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <signal.h>
#include <strings.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
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

typedef struct {
    const char *name;
    int num;
} sig_name_t;

static const sig_name_t k_signals[] = {
    { "HUP", SIGHUP },
    { "INT", SIGINT },
    { "QUIT", SIGQUIT },
    { "ILL", SIGILL },
    { "TRAP", SIGTRAP },
    { "ABRT", SIGABRT },
    { "BUS", SIGBUS },
    { "FPE", SIGFPE },
    { "KILL", SIGKILL },
    { "USR1", SIGUSR1 },
    { "SEGV", SIGSEGV },
    { "USR2", SIGUSR2 },
    { "PIPE", SIGPIPE },
    { "ALRM", SIGALRM },
    { "TERM", SIGTERM },
};

static const char *sig_name_from_num(int sig)
{
    for (size_t i = 0; i < sizeof(k_signals) / sizeof(k_signals[0]); ++i) {
        if (k_signals[i].num == sig) {
            return k_signals[i].name;
        }
    }
    return NULL;
}

static bool sig_num_from_name(const char *spec, int *out_sig)
{
    if (spec == NULL || out_sig == NULL) {
        return false;
    }

    while (*spec == ' ' || *spec == '\t') {
        ++spec;
    }
    if (*spec == '\0') {
        return false;
    }

    if (strncasecmp(spec, "SIG", 3) == 0) {
        spec += 3;
    }

    char name[16];
    size_t n = 0;
    while (*spec && n + 1 < sizeof(name)) {
        if (*spec == ' ' || *spec == '\t') {
            break;
        }
        name[n++] = (char)toupper((unsigned char)*spec);
        ++spec;
    }
    name[n] = '\0';

    if (n == 0) {
        return false;
    }

    if (isdigit((unsigned char)name[0])) {
        char *end = NULL;
        long v = strtol(name, &end, 10);
        if (end == name || *end != '\0' || v <= 0 || v > 255) {
            return false;
        }
        *out_sig = (int)v;
        return true;
    }

    for (size_t i = 0; i < sizeof(k_signals) / sizeof(k_signals[0]); ++i) {
        if (strcmp(name, k_signals[i].name) == 0) {
            *out_sig = k_signals[i].num;
            return true;
        }
    }
    return false;
}

static void print_signal_list(void)
{
    for (size_t i = 0; i < sizeof(k_signals) / sizeof(k_signals[0]); ++i) {
        if (i) {
            (void)write(STDOUT_FILENO, " ", 1);
        }
        (void)write(STDOUT_FILENO, k_signals[i].name, strlen(k_signals[i].name));
    }
    (void)write(STDOUT_FILENO, "\n", 1);
}

static void usage(void)
{
    eprintf("usage: kill [-s sigspec | -n signum | -sigspec] pid ...\n");
    eprintf("       kill -l [sigspec]\n");
}

static bool arg_is_signal_shortopt(const char *arg)
{
    if (arg == NULL) {
        return false;
    }
    if (arg[0] != '-' || arg[1] == '\0') {
        return false;
    }
    if (strcmp(arg, "--") == 0) {
        return false;
    }
    if (strcmp(arg, "-s") == 0 || strcmp(arg, "-n") == 0 || strcmp(arg, "-l") == 0) {
        return false;
    }
    return true;
}

static int handle_list_mode(int argc, char **argv, int idx)
{
    if (idx >= argc) {
        print_signal_list();
        return 0;
    }

    bool any_bad = false;
    for (int i = idx; i < argc; ++i) {
        const char *spec = argv[i];
        int sig = 0;
        if (!sig_num_from_name(spec, &sig)) {
            char *end = NULL;
            long v = strtol(spec, &end, 10);
            if (end != spec && *end == '\0') {
                if (v > 128) {
                    v -= 128;
                }
                sig = (int)v;
            } else {
                sig = 0;
            }
        }
        const char *name = sig_name_from_num(sig);
        if (name == NULL) {
            any_bad = true;
            continue;
        }
        (void)write(STDOUT_FILENO, name, strlen(name));
        if (i + 1 < argc) {
            (void)write(STDOUT_FILENO, "\n", 1);
        }
    }
    if (idx < argc) {
        (void)write(STDOUT_FILENO, "\n", 1);
    }
    return any_bad ? 1 : 0;
}

int main(int argc, char **argv)
{
    int sig = SIGTERM;
    bool list_mode = false;

    int i = 1;
    for (; i < argc; ++i) {
        const char *arg = argv[i];
        if (arg == NULL) {
            continue;
        }
        if (strcmp(arg, "--") == 0) {
            ++i;
            break;
        }
        if (strcmp(arg, "-h") == 0 || strcmp(arg, "--help") == 0) {
            usage();
            return 0;
        }
        if (strcmp(arg, "-l") == 0) {
            list_mode = true;
            ++i;
            break;
        }
        if (strcmp(arg, "-s") == 0) {
            if (i + 1 >= argc) {
                usage();
                return 1;
            }
            if (!sig_num_from_name(argv[i + 1], &sig)) {
                eprintf("kill: invalid signal: %s\n", argv[i + 1]);
                return 1;
            }
            i += 2;
            break;
        }
        if (strcmp(arg, "-n") == 0) {
            if (i + 1 >= argc) {
                usage();
                return 1;
            }
            char *end = NULL;
            long v = strtol(argv[i + 1], &end, 10);
            if (end == argv[i + 1] || *end != '\0' || v <= 0 || v > 255) {
                eprintf("kill: invalid signal number: %s\n", argv[i + 1]);
                return 1;
            }
            sig = (int)v;
            i += 2;
            break;
        }
        if (arg_is_signal_shortopt(arg)) {
            if (!sig_num_from_name(arg + 1, &sig)) {
                eprintf("kill: invalid signal: %s\n", arg + 1);
                return 1;
            }
            ++i;
            break;
        }
        break;
    }

    if (list_mode) {
        return handle_list_mode(argc, argv, i);
    }

    if (i >= argc) {
        usage();
        return 1;
    }

    bool failed = false;
    for (; i < argc; ++i) {
        const char *pid_s = argv[i];
        if (pid_s == NULL) {
            continue;
        }
        char *end = NULL;
        long pid_l = strtol(pid_s, &end, 10);
        if (end == pid_s || *end != '\0') {
            eprintf("kill: invalid pid: %s\n", pid_s);
            failed = true;
            continue;
        }
        if (pid_l <= 0 || pid_l > INT32_MAX) {
            eprintf("kill: invalid pid: %s\n", pid_s);
            failed = true;
            continue;
        }
        if (kill((pid_t)pid_l, sig) != 0) {
            eprintf("kill: %s: %s\n", pid_s, strerror(errno));
            failed = true;
        }
    }
    return failed ? 1 : 0;
}
