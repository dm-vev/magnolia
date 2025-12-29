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

#define WHO_USER (S_IRUSR | S_IWUSR | S_IXUSR)
#define WHO_GROUP (S_IRGRP | S_IWGRP | S_IXGRP)
#define WHO_OTHER (S_IROTH | S_IWOTH | S_IXOTH)
#define WHO_ALL (WHO_USER | WHO_GROUP | WHO_OTHER)

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
    eprintf("usage: mkdir [-p] [-m mode] directory ...\n");
}

static int parse_mode_octal(const char *s, mode_t *out)
{
    if (s == NULL || *s == '\0') {
        return -1;
    }
    for (const char *p = s; *p; ++p) {
        if (*p < '0' || *p > '7') {
            return -1;
        }
    }
    errno = 0;
    char *end = NULL;
    unsigned long value = strtoul(s, &end, 8);
    if (errno != 0 || end == s || *end != '\0') {
        return -1;
    }
    *out = (mode_t)value;
    return 0;
}

static mode_t who_mask(char c)
{
    switch (c) {
    case 'u':
        return WHO_USER;
    case 'g':
        return WHO_GROUP;
    case 'o':
        return WHO_OTHER;
    case 'a':
        return WHO_ALL;
    default:
        return 0;
    }
}

static int class_shift(char c)
{
    switch (c) {
    case 'u':
        return 6;
    case 'g':
        return 3;
    case 'o':
        return 0;
    default:
        return 0;
    }
}

static mode_t class_bits(mode_t mode, char c)
{
    switch (c) {
    case 'u':
        return mode & WHO_USER;
    case 'g':
        return mode & WHO_GROUP;
    case 'o':
        return mode & WHO_OTHER;
    default:
        return 0;
    }
}

static mode_t shift_bits(mode_t bits, int from, int to)
{
    if (from > to) {
        return bits >> (from - to);
    }
    if (from < to) {
        return bits << (to - from);
    }
    return bits;
}

static mode_t perm_bits_for_who(mode_t who, char perm)
{
    mode_t bits = 0;
    if (perm == 'r') {
        if (who & WHO_USER) {
            bits |= S_IRUSR;
        }
        if (who & WHO_GROUP) {
            bits |= S_IRGRP;
        }
        if (who & WHO_OTHER) {
            bits |= S_IROTH;
        }
        return bits;
    }
    if (perm == 'w') {
        if (who & WHO_USER) {
            bits |= S_IWUSR;
        }
        if (who & WHO_GROUP) {
            bits |= S_IWGRP;
        }
        if (who & WHO_OTHER) {
            bits |= S_IWOTH;
        }
        return bits;
    }
    if (perm == 'x') {
        if (who & WHO_USER) {
            bits |= S_IXUSR;
        }
        if (who & WHO_GROUP) {
            bits |= S_IXGRP;
        }
        if (who & WHO_OTHER) {
            bits |= S_IXOTH;
        }
        return bits;
    }
    return 0;
}

static mode_t copy_bits(mode_t mode, char from, mode_t who)
{
    mode_t src = class_bits(mode, from);
    int src_shift = class_shift(from);
    mode_t out = 0;
    if (who & WHO_USER) {
        out |= shift_bits(src, src_shift, class_shift('u'));
    }
    if (who & WHO_GROUP) {
        out |= shift_bits(src, src_shift, class_shift('g'));
    }
    if (who & WHO_OTHER) {
        out |= shift_bits(src, src_shift, class_shift('o'));
    }
    return out;
}

static int parse_mode_symbolic(const char *s, mode_t *out)
{
    if (s == NULL || *s == '\0') {
        return -1;
    }

    /* BSD mkdir applies symbolic modes relative to a 0777 base. */
    mode_t mode = WHO_ALL;
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

        mode_t cur_mode = mode;
        mode_t perms = 0;
        mode_t special = 0;
        bool saw_perm = false;
        while (*p && *p != ',') {
            char perm = *p;
            saw_perm = true;
            switch (perm) {
            case 'r':
            case 'w':
            case 'x':
                perms |= perm_bits_for_who(who, perm);
                break;
            case 'X':
                perms |= perm_bits_for_who(who, 'x');
                break;
            case 's':
                if (who & WHO_USER) {
                    special |= S_ISUID;
                }
                if (who & WHO_GROUP) {
                    special |= S_ISGID;
                }
                break;
            case 't':
                special |= S_ISVTX;
                break;
            case 'u':
            case 'g':
            case 'o':
                perms |= copy_bits(cur_mode, perm, who);
                break;
            default:
                return -1;
            }
            p++;
        }

        if (op == '=') {
            mode_t clear_mask = who;
            if (who & WHO_USER) {
                clear_mask |= S_ISUID;
            }
            if (who & WHO_GROUP) {
                clear_mask |= S_ISGID;
            }
            if (who & WHO_OTHER) {
                clear_mask |= S_ISVTX;
            }
            mode &= ~clear_mask;
        }

        if (!saw_perm) {
            if (op == '+') {
                /* no-op */
            } else if (op == '-') {
                /* no-op */
            } else {
                /* cleared by '=' above */
            }
        } else if (op == '+') {
            mode |= perms | special;
        } else if (op == '-') {
            mode &= ~(perms | special);
        } else {
            mode |= perms | special;
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

static int mkdir_component(const char *path, mode_t mode, bool allow_existing, bool apply_mode, bool require_dir)
{
    if (mkdir(path, apply_mode ? mode : 0777) == 0) {
        if (apply_mode && chmod(path, mode) != 0) {
            return -1;
        }
        return 0;
    }
    if (allow_existing && errno == EEXIST) {
        struct stat st;
        if (stat(path, &st) == 0) {
            if (S_ISDIR(st.st_mode)) {
                return 0;
            }
            errno = require_dir ? ENOTDIR : EEXIST;
        }
    }
    return -1;
}

static int mkdir_parents(const char *path, mode_t final_mode, bool apply_mode)
{
    size_t len = strlen(path);
    if (len == 0) {
        errno = ENOENT;
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

    if (len == 1 && buf[0] == '/') {
        free(buf);
        return 0;
    }

    for (char *p = buf + 1; *p; ++p) {
        if (*p != '/') {
            continue;
        }
        *p = '\0';
        if (mkdir_component(buf, 0777, true, false, true) != 0) {
            free(buf);
            return -1;
        }
        *p = '/';
        while (p[1] == '/') {
            p++;
        }
    }

    int rc = mkdir_component(buf, apply_mode ? final_mode : 0777, true, apply_mode, false);
    free(buf);
    return rc;
}

int main(int argc, char **argv)
{
    opterr = 0;
    bool parents = false;
    mode_t mode = 0;
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
            if (optopt == 'm') {
                eprintf("mkdir: option requires an argument -- %c\n", optopt);
            } else {
                eprintf("mkdir: illegal option -- %c\n", optopt);
            }
            usage();
            return 1;
        }
    }

    if (optind >= argc) {
        usage();
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
            rc = mkdir_parents(path, mode, mode_set);
        } else {
            rc = mkdir_component(path, mode, false, mode_set, false);
        }
        if (rc != 0) {
            eprintf("mkdir: %s: %s\n", path, strerror(errno));
            failed = 1;
        }
    }
    return failed;
}
