#include <errno.h>
#include <stdbool.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

extern char **environ;
int setenv(const char *name, const char *value, int overwrite);
int unsetenv(const char *name);

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

static int streq(const char *a, const char *b)
{
    return a && b && strcmp(a, b) == 0;
}

static void print_help(void)
{
    printf("usage: env [OPTION]... [-] [NAME=VALUE]... [COMMAND [ARG]...]\n");
    printf("  -i, -           start with an empty environment\n");
    printf("  -u NAME         remove NAME from the environment\n");
    printf("      --help      display this help and exit\n");
    printf("      --version   output version information and exit\n");
    printf("note: COMMAND execution is not implemented yet in Magnolia applets.\n");
}

static void print_version(void)
{
    printf("env (%s)\n", g_version);
}

static bool is_assignment(const char *s)
{
    if (!s) {
        return false;
    }
    const char *eq = strchr(s, '=');
    if (!eq || eq == s) {
        return false;
    }
    return true;
}

static int write_all(int fd, const void *buf, size_t len)
{
    const unsigned char *p = (const unsigned char *)buf;
    size_t off = 0;
    while (off < len) {
        ssize_t w = write(fd, p + off, len - off);
        if (w < 0) {
            return -1;
        }
        off += (size_t)w;
    }
    return 0;
}

static int print_env(void)
{
    if (!environ) {
        return 0;
    }
    for (size_t i = 0; environ[i] != NULL; ++i) {
        const char *line = environ[i];
        if (write_all(STDOUT_FILENO, line, strlen(line)) != 0 ||
            write_all(STDOUT_FILENO, "\n", 1) != 0) {
            eprintf("env: write: %s\n", strerror(errno));
            return 1;
        }
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

    bool clear_env = false;
    int i = 1;
    while (i < argc && argv[i]) {
        if (streq(argv[i], "-") || streq(argv[i], "-i")) {
            clear_env = true;
            i++;
            continue;
        }
        if (streq(argv[i], "-u")) {
            if (i + 1 >= argc || !argv[i + 1]) {
                eprintf("env: option requires an argument -- u\n");
                return 125;
            }
            if (unsetenv(argv[i + 1]) != 0) {
                eprintf("env: unsetenv %s: %s\n", argv[i + 1], strerror(errno));
                return 125;
            }
            i += 2;
            continue;
        }
        if (argv[i][0] == '-' && argv[i][1] != '\0') {
            eprintf("env: invalid option: %s\n", argv[i]);
            eprintf("try 'env --help'\n");
            return 125;
        }
        break;
    }

    if (clear_env) {
        while (environ && environ[0]) {
            const char *kv = environ[0];
            const char *eq = strchr(kv, '=');
            if (!eq) {
                break;
            }
            size_t key_len = (size_t)(eq - kv);
            char key[64];
            if (key_len >= sizeof(key)) {
                eprintf("env: variable name too long\n");
                return 125;
            }
            memcpy(key, kv, key_len);
            key[key_len] = '\0';
            (void)unsetenv(key);
        }
    }

    while (i < argc && argv[i] && is_assignment(argv[i])) {
        const char *eq = strchr(argv[i], '=');
        size_t key_len = (size_t)(eq - argv[i]);
        char key[64];
        if (key_len >= sizeof(key)) {
            eprintf("env: variable name too long: %s\n", argv[i]);
            return 125;
        }
        memcpy(key, argv[i], key_len);
        key[key_len] = '\0';
        const char *val = eq + 1;
        if (setenv(key, val, 1) != 0) {
            eprintf("env: setenv %s: %s\n", key, strerror(errno));
            return 125;
        }
        i++;
    }

    if (i < argc && argv[i]) {
        eprintf("env: command execution not supported: %s\n", argv[i]);
        return 127;
    }

    return print_env();
}
