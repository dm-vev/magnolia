#include <errno.h>
#include <stdbool.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
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
    printf("usage: test EXPRESSION\n");
    printf("   or: test [--help] [--version]\n");
    printf("supported: -e -f -d -r -w -x -n -z, = !=, -eq -ne -gt -ge -lt -le, !\n");
}

static void print_version(void)
{
    printf("test (%s)\n", g_version);
}

static int file_test(const char *op, const char *path, bool *out)
{
    struct stat st;
    if (streq(op, "-e")) {
        *out = (stat(path, &st) == 0);
        return 0;
    }
    if (streq(op, "-f")) {
        *out = (stat(path, &st) == 0) && S_ISREG(st.st_mode);
        return 0;
    }
    if (streq(op, "-d")) {
        *out = (stat(path, &st) == 0) && S_ISDIR(st.st_mode);
        return 0;
    }
    if (streq(op, "-r")) {
        *out = (access(path, R_OK) == 0);
        return 0;
    }
    if (streq(op, "-w")) {
        *out = (access(path, W_OK) == 0);
        return 0;
    }
    if (streq(op, "-x")) {
        *out = (access(path, X_OK) == 0);
        return 0;
    }
    return -1;
}

static int string_test(const char *op, const char *s, bool *out)
{
    if (streq(op, "-n")) {
        *out = (s && s[0] != '\0');
        return 0;
    }
    if (streq(op, "-z")) {
        *out = (!s || s[0] == '\0');
        return 0;
    }
    return -1;
}

static int parse_int(const char *s, long *out)
{
    if (!s || *s == '\0') {
        return -1;
    }
    char *end = NULL;
    errno = 0;
    long v = strtol(s, &end, 10);
    if (errno != 0 || end == s || *end != '\0') {
        return -1;
    }
    *out = v;
    return 0;
}

static int int_cmp(const char *a, const char *op, const char *b, bool *out)
{
    long ia, ib;
    if (parse_int(a, &ia) != 0 || parse_int(b, &ib) != 0) {
        return -1;
    }
    if (streq(op, "-eq")) {
        *out = (ia == ib);
        return 0;
    }
    if (streq(op, "-ne")) {
        *out = (ia != ib);
        return 0;
    }
    if (streq(op, "-gt")) {
        *out = (ia > ib);
        return 0;
    }
    if (streq(op, "-ge")) {
        *out = (ia >= ib);
        return 0;
    }
    if (streq(op, "-lt")) {
        *out = (ia < ib);
        return 0;
    }
    if (streq(op, "-le")) {
        *out = (ia <= ib);
        return 0;
    }
    return -1;
}

static int eval(int argc, char **argv, bool *out)
{
    if (argc <= 0) {
        *out = false;
        return 0;
    }

    if (argc >= 1 && streq(argv[0], "!")) {
        bool inner = false;
        int rc = eval(argc - 1, argv + 1, &inner);
        if (rc != 0) {
            return rc;
        }
        *out = !inner;
        return 0;
    }

    if (argc == 1) {
        *out = (argv[0] && argv[0][0] != '\0');
        return 0;
    }

    if (argc == 2) {
        bool r = false;
        if (string_test(argv[0], argv[1], &r) == 0 || file_test(argv[0], argv[1], &r) == 0) {
            *out = r;
            return 0;
        }
        return 2;
    }

    if (argc == 3) {
        if (streq(argv[1], "=")) {
            *out = strcmp(argv[0], argv[2]) == 0;
            return 0;
        }
        if (streq(argv[1], "!=")) {
            *out = strcmp(argv[0], argv[2]) != 0;
            return 0;
        }
        bool r = false;
        if (int_cmp(argv[0], argv[1], argv[2], &r) == 0) {
            *out = r;
            return 0;
        }
        return 2;
    }

    return 2;
}

int main(int argc, char **argv)
{
    if (argc == 2 && argv[1]) {
        if (streq(argv[1], "--help")) {
            print_help();
            return 0;
        }
        if (streq(argv[1], "--version")) {
            print_version();
            return 0;
        }
    }

    if (argc <= 1) {
        return 1;
    }

    bool result = false;
    int rc = eval(argc - 1, argv + 1, &result);
    if (rc == 2) {
        eprintf("test: syntax error\n");
        return 2;
    }
    return result ? 0 : 1;
}

