#include <errno.h>
#include <getopt.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
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

static int parse_mode_octal(const char *s, mode_t *out)
{
    if (s == NULL || *s == '\0') {
        return -1;
    }
    mode_t value = 0;
    for (const char *p = s; *p; ++p) {
        if (*p < '0' || *p > '7') {
            return -1;
        }
        value = (mode_t)((value << 3) | (mode_t)(*p - '0'));
    }
    *out = value;
    return 0;
}

static mode_t who_mask(char c)
{
    switch (c) {
    case 'u':
        return S_IRUSR | S_IWUSR | S_IXUSR;
    case 'g':
        return S_IRGRP | S_IWGRP | S_IXGRP;
    case 'o':
        return S_IROTH | S_IWOTH | S_IXOTH;
    case 'a':
        return S_IRUSR | S_IWUSR | S_IXUSR | S_IRGRP | S_IWGRP | S_IXGRP | S_IROTH | S_IWOTH | S_IXOTH;
    default:
        return 0;
    }
}

static mode_t perm_bits_for_who(char who, char perm)
{
    if (perm == 'r') {
        return (who == 'u') ? S_IRUSR : (who == 'g') ? S_IRGRP : S_IROTH;
    }
    if (perm == 'w') {
        return (who == 'u') ? S_IWUSR : (who == 'g') ? S_IWGRP : S_IWOTH;
    }
    if (perm == 'x') {
        return (who == 'u') ? S_IXUSR : (who == 'g') ? S_IXGRP : S_IXOTH;
    }
    return 0;
}

static int parse_mode_symbolic(const char *s, mode_t *out)
{
    if (s == NULL || *s == '\0') {
        return -1;
    }

    mode_t mode = 0777;
    const char *p = s;
    while (*p) {
        mode_t who = 0;
        while (*p == 'u' || *p == 'g' || *p == 'o' || *p == 'a') {
            who |= who_mask(*p);
            p++;
        }
        if (who == 0) {
            who = who_mask('a');
        }

        char op = *p;
        if (op != '+' && op != '-' && op != '=') {
            return -1;
        }
        p++;

        mode_t perms_u = 0;
        mode_t perms_g = 0;
        mode_t perms_o = 0;
        while (*p == 'r' || *p == 'w' || *p == 'x') {
            char perm = *p;
            perms_u |= perm_bits_for_who('u', perm);
            perms_g |= perm_bits_for_who('g', perm);
            perms_o |= perm_bits_for_who('o', perm);
            p++;
        }

        if (op == '=') {
            mode &= ~who;
        }

        if (who & who_mask('u')) {
            if (op == '+') {
                mode |= perms_u;
            } else if (op == '-') {
                mode &= ~perms_u;
            } else {
                mode |= perms_u;
            }
        }
        if (who & who_mask('g')) {
            if (op == '+') {
                mode |= perms_g;
            } else if (op == '-') {
                mode &= ~perms_g;
            } else {
                mode |= perms_g;
            }
        }
        if (who & who_mask('o')) {
            if (op == '+') {
                mode |= perms_o;
            } else if (op == '-') {
                mode &= ~perms_o;
            } else {
                mode |= perms_o;
            }
        }

        if (*p == ',') {
            p++;
            continue;
        }
        if (*p != '\0') {
            return -1;
        }
    }

    *out = mode;
    return 0;
}

static int parse_mode(const char *s, mode_t *out)
{
    if (parse_mode_octal(s, out) == 0) {
        return 0;
    }
    return parse_mode_symbolic(s, out);
}

static int mkdir_one(const char *path, mode_t mode, bool allow_existing)
{
    if (mkdir(path, mode) == 0) {
        return 0;
    }
    if (allow_existing && errno == EEXIST) {
        struct stat st;
        if (stat(path, &st) == 0 && S_ISDIR(st.st_mode)) {
            return 0;
        }
    }
    return -1;
}

static int mkdir_parents(const char *path, mode_t final_mode)
{
    size_t len = strlen(path);
    if (len == 0) {
        errno = EINVAL;
        return -1;
    }

    char *buf = (char *)malloc(len + 1);
    if (!buf) {
        errno = ENOMEM;
        return -1;
    }
    memcpy(buf, path, len + 1);

    while (len > 1 && buf[len - 1] == '/') {
        buf[len - 1] = '\0';
        len--;
    }

    for (char *p = buf + 1; *p; ++p) {
        if (*p != '/') {
            continue;
        }
        *p = '\0';
        if (mkdir_one(buf, 0777, true) != 0) {
            free(buf);
            return -1;
        }
        *p = '/';
    }

    int rc = mkdir_one(buf, final_mode, true);
    free(buf);
    return rc;
}

int main(int argc, char **argv)
{
    bool parents = false;
    mode_t mode = 0777;
    bool mode_set = false;

    int opt;
    while ((opt = getopt(argc, argv, "pm:")) != -1) {
        switch (opt) {
        case 'p':
            parents = true;
            break;
        case 'm':
            if (parse_mode(optarg, &mode) != 0) {
                eprintf("mkdir: invalid mode: %s\n", optarg ? optarg : "");
                return 1;
            }
            mode_set = true;
            break;
        default:
            eprintf("usage: mkdir [-p] [-m mode] dir ...\n");
            return 1;
        }
    }

    if (optind >= argc) {
        eprintf("mkdir: missing operand\n");
        return 1;
    }

    int failed = 0;
    for (int i = optind; i < argc; ++i) {
        const char *path = argv[i];
        if (path == NULL) {
            continue;
        }

        int rc;
        if (parents) {
            rc = mkdir_parents(path, mode_set ? mode : 0777);
        } else {
            rc = mkdir_one(path, mode_set ? mode : 0777, false);
        }
        if (rc != 0) {
            eprintf("mkdir: %s: %s\n", path, strerror(errno));
            failed = 1;
        }
    }
    return failed;
}
