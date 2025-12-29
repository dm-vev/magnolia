#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#include <ctype.h>
#include <errno.h>
#include <signal.h>
#include <strings.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

#ifndef NSIG
#define NSIG 32
#endif

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

static const sig_name_t k_signal_names[] = {
#ifdef SIGHUP
    { "HUP", SIGHUP },
#endif
#ifdef SIGINT
    { "INT", SIGINT },
#endif
#ifdef SIGQUIT
    { "QUIT", SIGQUIT },
#endif
#ifdef SIGILL
    { "ILL", SIGILL },
#endif
#ifdef SIGTRAP
    { "TRAP", SIGTRAP },
#endif
#ifdef SIGABRT
    { "ABRT", SIGABRT },
#endif
#ifdef SIGEMT
    { "EMT", SIGEMT },
#endif
#ifdef SIGFPE
    { "FPE", SIGFPE },
#endif
#ifdef SIGKILL
    { "KILL", SIGKILL },
#endif
#ifdef SIGBUS
    { "BUS", SIGBUS },
#endif
#ifdef SIGSEGV
    { "SEGV", SIGSEGV },
#endif
#ifdef SIGSYS
    { "SYS", SIGSYS },
#endif
#ifdef SIGPIPE
    { "PIPE", SIGPIPE },
#endif
#ifdef SIGALRM
    { "ALRM", SIGALRM },
#endif
#ifdef SIGTERM
    { "TERM", SIGTERM },
#endif
#ifdef SIGURG
    { "URG", SIGURG },
#endif
#ifdef SIGSTOP
    { "STOP", SIGSTOP },
#endif
#ifdef SIGTSTP
    { "TSTP", SIGTSTP },
#endif
#ifdef SIGCONT
    { "CONT", SIGCONT },
#endif
#ifdef SIGCHLD
    { "CHLD", SIGCHLD },
#endif
#ifdef SIGTTIN
    { "TTIN", SIGTTIN },
#endif
#ifdef SIGTTOU
    { "TTOU", SIGTTOU },
#endif
#ifdef SIGIO
    { "IO", SIGIO },
#elif defined(SIGPOLL)
    { "IO", SIGPOLL },
#endif
#ifdef SIGXCPU
    { "XCPU", SIGXCPU },
#endif
#ifdef SIGXFSZ
    { "XFSZ", SIGXFSZ },
#endif
#ifdef SIGVTALRM
    { "VTALRM", SIGVTALRM },
#endif
#ifdef SIGPROF
    { "PROF", SIGPROF },
#endif
#ifdef SIGWINCH
    { "WINCH", SIGWINCH },
#endif
#ifdef SIGINFO
    { "INFO", SIGINFO },
#endif
#ifdef SIGUSR1
    { "USR1", SIGUSR1 },
#endif
#ifdef SIGUSR2
    { "USR2", SIGUSR2 },
#endif
};

// TODO: Verify BSD alias support for SIGIOT/SIGCLD on target libc.
static const sig_name_t k_signal_aliases[] = {
#ifdef SIGIOT
    { "IOT", SIGIOT },
#endif
#ifdef SIGCLD
    { "CLD", SIGCLD },
#endif
};

static const char *g_signal_names[NSIG];
static bool g_signal_names_init;

// Build a stable signal number -> name table for BSD-style output.
static void init_signal_names(void)
{
    if (g_signal_names_init) {
        return;
    }
    for (int i = 0; i < NSIG; ++i) {
        g_signal_names[i] = NULL;
    }
    g_signal_names[0] = "0";
    for (size_t i = 0; i < sizeof(k_signal_names) / sizeof(k_signal_names[0]); ++i) {
        int sig = k_signal_names[i].num;
        if (sig > 0 && sig < NSIG && g_signal_names[sig] == NULL) {
            g_signal_names[sig] = k_signal_names[i].name;
        }
    }
    g_signal_names_init = true;
}

static const char *sig_name_from_num(int sig)
{
    init_signal_names();
    if (sig < 0 || sig >= NSIG) {
        return NULL;
    }
    return g_signal_names[sig];
}

static bool parse_signal_number(const char *spec, int *out_sig)
{
    if (spec == NULL || out_sig == NULL) {
        return false;
    }
    errno = 0;
    char *end = NULL;
    long v = strtol(spec, &end, 10);
    if (end == spec || *end != '\0' || errno == ERANGE) {
        return false;
    }
    if (v < 0 || v >= NSIG) {
        return false;
    }
    *out_sig = (int)v;
    return true;
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
    while (*spec) {
        if (*spec == ' ' || *spec == '\t') {
            break;
        }
        if (n + 1 >= sizeof(name)) {
            return false;
        }
        name[n++] = (char)toupper((unsigned char)*spec);
        ++spec;
    }
    name[n] = '\0';

    if (n == 0) {
        return false;
    }

    if (isdigit((unsigned char)name[0]) || name[0] == '+') {
        return parse_signal_number(name, out_sig);
    }

    init_signal_names();
    for (size_t i = 0; i < sizeof(k_signal_names) / sizeof(k_signal_names[0]); ++i) {
        if (strcmp(name, k_signal_names[i].name) == 0) {
            *out_sig = k_signal_names[i].num;
            return true;
        }
    }
    for (size_t i = 0; i < sizeof(k_signal_aliases) / sizeof(k_signal_aliases[0]); ++i) {
        if (strcmp(name, k_signal_aliases[i].name) == 0) {
            *out_sig = k_signal_aliases[i].num;
            return true;
        }
    }
    return false;
}

static void print_signal_list(void)
{
    init_signal_names();
    bool first = true;
    for (int sig = 1; sig < NSIG; ++sig) {
        const char *name = g_signal_names[sig];
        if (!name) {
            continue;
        }
        if (!first) {
            (void)write(STDOUT_FILENO, " ", 1);
        }
        (void)write(STDOUT_FILENO, name, strlen(name));
        first = false;
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
            errno = 0;
            long v = strtol(spec, &end, 10);
            if (end != spec && *end == '\0' && errno != ERANGE) {
                // POSIX: exit status is 128 + signal number.
                if (v > 128) {
                    v -= 128;
                }
                if (v < 0 || v >= NSIG) {
                    sig = -1;
                } else {
                    sig = (int)v;
                }
            } else {
                sig = -1;
            }
        }
        const char *name = sig_name_from_num(sig);
        if (name == NULL) {
            eprintf("kill: invalid signal: %s\n", spec ? spec : "");
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
    if (i < argc && argv[i] && strcmp(argv[i], "--") == 0) {
        ++i;
    } else if (i < argc && argv[i] && strcmp(argv[i], "-l") == 0) {
        list_mode = true;
        ++i;
    } else if (i < argc && argv[i] && strcmp(argv[i], "-s") == 0) {
        if (i + 1 >= argc) {
            usage();
            return 1;
        }
        if (!sig_num_from_name(argv[i + 1], &sig)) {
            eprintf("kill: invalid signal: %s\n", argv[i + 1]);
            return 1;
        }
        i += 2;
    } else if (i < argc && argv[i] && strcmp(argv[i], "-n") == 0) {
        if (i + 1 >= argc) {
            usage();
            return 1;
        }
        if (!parse_signal_number(argv[i + 1], &sig)) {
            eprintf("kill: invalid signal number: %s\n", argv[i + 1]);
            return 1;
        }
        i += 2;
    } else if (i < argc && argv[i] && arg_is_signal_shortopt(argv[i])) {
        if (!sig_num_from_name(argv[i] + 1, &sig)) {
            eprintf("kill: invalid signal: %s\n", argv[i] + 1);
            return 1;
        }
        ++i;
    }

    if (list_mode) {
        return handle_list_mode(argc, argv, i);
    }

    if (i < argc && argv[i] && strcmp(argv[i], "--") == 0) {
        ++i;
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
        errno = 0;
        long pid_l = strtol(pid_s, &end, 10);
        if (end == pid_s || *end != '\0' || errno == ERANGE) {
            eprintf("kill: invalid pid: %s\n", pid_s);
            failed = true;
            continue;
        }
        pid_t pid = (pid_t)pid_l;
        if ((long)pid != pid_l) {
            eprintf("kill: invalid pid: %s\n", pid_s);
            failed = true;
            continue;
        }
        if (kill(pid, sig) != 0) {
            eprintf("kill: %s: %s\n", pid_s, strerror(errno));
            failed = true;
        }
    }
    return failed ? 1 : 0;
}
