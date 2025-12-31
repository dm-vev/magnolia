#include <dirent.h>
#include <errno.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

typedef struct {
    bool all;
    bool list_long;
    bool list_dirs;
} ls_opts_t;

/* BSD reference: FreeBSD ls(1). */

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

static int oprintf(const char *fmt, ...)
{
    char stack[256];
    va_list ap;
    va_start(ap, fmt);
    va_list ap2;
    va_copy(ap2, ap);
    int n = vsnprintf(stack, sizeof(stack), fmt, ap);
    va_end(ap);
    if (n < 0) {
        va_end(ap2);
        errno = EIO;
        return -1;
    }
    size_t len = (size_t)n;
    if (len < sizeof(stack)) {
        va_end(ap2);
        return write_all(STDOUT_FILENO, stack, len);
    }
    if (len == SIZE_MAX) {
        va_end(ap2);
        errno = EOVERFLOW;
        return -1;
    }
    char *buf = (char *)malloc(len + 1);
    if (buf == NULL) {
        va_end(ap2);
        errno = ENOMEM;
        return -1;
    }
    int n2 = vsnprintf(buf, len + 1, fmt, ap2);
    va_end(ap2);
    if (n2 < 0 || (size_t)n2 != len) {
        free(buf);
        errno = EIO;
        return -1;
    }
    int rc = write_all(STDOUT_FILENO, buf, len);
    free(buf);
    return rc;
}

static int cmp_strptr(const void *a, const void *b)
{
    const char *const *sa = (const char *const *)a;
    const char *const *sb = (const char *const *)b;
    return strcmp(*sa, *sb);
}

static char *xstrdup(const char *s)
{
    size_t len = strlen(s) + 1;
    char *out = (char *)malloc(len);
    if (!out) {
        return NULL;
    }
    memcpy(out, s, len);
    return out;
}

static int lstat_compat(const char *path, struct stat *st)
{
#ifdef ESP_PLATFORM
    return stat(path, st);
#else
    return lstat(path, st);
#endif
}

static void mode_string(mode_t mode, char out[11])
{
    if (S_ISDIR(mode)) {
        out[0] = 'd';
    } else if (S_ISCHR(mode)) {
        out[0] = 'c';
    } else if (S_ISBLK(mode)) {
        out[0] = 'b';
    } else if (S_ISLNK(mode)) {
        out[0] = 'l';
    } else if (S_ISFIFO(mode)) {
        out[0] = 'p';
    } else if (S_ISSOCK(mode)) {
        out[0] = 's';
    } else {
        out[0] = '-';
    }
    out[1] = (mode & S_IRUSR) ? 'r' : '-';
    out[2] = (mode & S_IWUSR) ? 'w' : '-';
    out[3] = (mode & S_IXUSR) ? 'x' : '-';
    out[4] = (mode & S_IRGRP) ? 'r' : '-';
    out[5] = (mode & S_IWGRP) ? 'w' : '-';
    out[6] = (mode & S_IXGRP) ? 'x' : '-';
    out[7] = (mode & S_IROTH) ? 'r' : '-';
    out[8] = (mode & S_IWOTH) ? 'w' : '-';
    out[9] = (mode & S_IXOTH) ? 'x' : '-';
    out[10] = '\0';
}

static int ls_print(const char *display, const char *path, const ls_opts_t *opts)
{
    if (!opts->list_long) {
        if (oprintf("%s\n", display) != 0) {
            eprintf("ls: stdout: %s\n", strerror(errno));
            return 1;
        }
        return 0;
    }

    struct stat st;
    if (lstat_compat(path, &st) != 0) {
        eprintf("ls: %s: %s\n", path, strerror(errno));
        return 1;
    }

    char mode[11];
    mode_string(st.st_mode, mode);

    char timebuf[32] = {0};
    struct tm *tm = localtime(&st.st_mtime);
    if (tm != NULL) {
        (void)strftime(timebuf, sizeof(timebuf), "%Y-%m-%d %H:%M", tm);
    } else {
        strncpy(timebuf, "????????????", sizeof(timebuf) - 1);
    }

    if (oprintf("%s %8" PRIdMAX " %s %s\n", mode, (intmax_t)st.st_size, timebuf, display) != 0) {
        eprintf("ls: stdout: %s\n", strerror(errno));
        return 1;
    }
    return 0;
}

static int ls_dir(const char *path, const ls_opts_t *opts)
{
    struct stat st;
    size_t plen = strlen(path);
    bool ends_with_slash = (plen > 0 && path[plen - 1] == '/');
    /* Preserve BSD behavior: symlinks are listed unless the path ends with '/'. */
    if ((ends_with_slash ? stat(path, &st) : lstat_compat(path, &st)) != 0) {
        eprintf("ls: %s: %s\n", path, strerror(errno));
        return 1;
    }

    if (!S_ISDIR(st.st_mode) || opts->list_dirs) {
        return ls_print(path, path, opts);
    }

    DIR *dir = opendir(path);
    if (dir == NULL) {
        eprintf("ls: %s: %s\n", path, strerror(errno));
        return 1;
    }

    size_t cap = 0;
    size_t count = 0;
    char **names = NULL;

    struct dirent *ent;
    while (1) {
        errno = 0;
        ent = readdir(dir);
        if (ent == NULL) {
            break;
        }
        if (!opts->all && ent->d_name[0] == '.') {
            continue;
        }
        if (count == cap) {
            size_t next = cap ? cap * 2 : 32;
            if (next > SIZE_MAX / sizeof(*names)) {
                eprintf("ls: %s: out of memory\n", path);
                for (size_t i = 0; i < count; ++i) {
                    free(names[i]);
                }
                free(names);
                (void)closedir(dir);
                return 1;
            }
            char **tmp = (char **)realloc(names, next * sizeof(*names));
            if (!tmp) {
                eprintf("ls: %s: out of memory\n", path);
                for (size_t i = 0; i < count; ++i) {
                    free(names[i]);
                }
                free(names);
                (void)closedir(dir);
                return 1;
            }
            names = tmp;
            cap = next;
        }
        names[count] = xstrdup(ent->d_name);
        if (!names[count]) {
            eprintf("ls: %s: out of memory\n", path);
            for (size_t i = 0; i < count; ++i) {
                free(names[i]);
            }
            free(names);
            (void)closedir(dir);
            return 1;
        }
        count++;
    }
    if (ent == NULL && errno != 0) {
        eprintf("ls: %s: %s\n", path, strerror(errno));
        (void)closedir(dir);
        for (size_t i = 0; i < count; ++i) {
            free(names[i]);
        }
        free(names);
        return 1;
    }
    (void)closedir(dir);

    qsort(names, count, sizeof(*names), cmp_strptr);

    int failed = 0;
    for (size_t i = 0; i < count; ++i) {
        char *name = names[i];
        if (!name) {
            continue;
        }
        size_t plen = strlen(path);
        bool need_slash = (plen > 0 && path[plen - 1] != '/');
        size_t name_len = strlen(name);
        size_t slash = need_slash ? 1 : 0;
        if (name_len > SIZE_MAX - slash - 1) {
            errno = ENAMETOOLONG;
            eprintf("ls: %s/%s: %s\n", path, name, strerror(errno));
            failed = 1;
            free(name);
            continue;
        }
        size_t extra = slash + name_len + 1;
        if (plen > SIZE_MAX - extra) {
            errno = ENAMETOOLONG;
            eprintf("ls: %s/%s: %s\n", path, name, strerror(errno));
            failed = 1;
            free(name);
            continue;
        }
        size_t full_len = plen + extra;
        char *full = (char *)malloc(full_len);
        if (!full) {
            eprintf("ls: %s/%s: out of memory\n", path, name);
            failed = 1;
            free(name);
            continue;
        }
        memcpy(full, path, plen);
        size_t off = plen;
        if (need_slash) {
            full[off++] = '/';
        }
        strcpy(full + off, name);

        failed |= ls_print(name, full, opts);
        free(full);
        free(name);
    }
    free(names);
    return failed ? 1 : 0;
}

int main(int argc, char **argv)
{
    ls_opts_t opts = {0};
    /* FreeBSD ls(1) compatibility: support the common -a/-d/-l/-1 set. */
    const char *paths[argc > 1 ? (size_t)argc - 1 : 1];
    int n_paths = 0;
    bool parse_opts = true;

    for (int i = 1; i < argc; ++i) {
        const char *arg = argv[i];
        if (arg == NULL) {
            continue;
        }
        if (parse_opts && arg[0] == '-' && arg[1] != '\0') {
            if (strcmp(arg, "--") == 0) {
                parse_opts = false;
                continue;
            }
            for (size_t j = 1; arg[j] != '\0'; ++j) {
                switch (arg[j]) {
                case 'a':
                    opts.all = true;
                    break;
                case 'l':
                    opts.list_long = true;
                    break;
                case 'd':
                    opts.list_dirs = true;
                    break;
                case '1':
                    /* default */
                    break;
                default:
                    eprintf("usage: ls [-a] [-d] [-l] [file ...]\n");
                    return 1;
                }
            }
            continue;
        }
        paths[n_paths++] = arg;
    }

    if (n_paths <= 0) {
        return ls_dir(".", &opts);
    }

    int failed = 0;
    for (int i = 0; i < n_paths; ++i) {
        const char *path = paths[i];
        if (n_paths > 1) {
            struct stat st;
            size_t plen = strlen(path);
            bool ends_with_slash = (plen > 0 && path[plen - 1] == '/');
            if ((ends_with_slash ? stat(path, &st) : lstat_compat(path, &st)) == 0 &&
                S_ISDIR(st.st_mode) && !opts.list_dirs) {
                if (oprintf("%s:\n", path) != 0) {
                    eprintf("ls: stdout: %s\n", strerror(errno));
                    return 1;
                }
            }
        }

        failed |= ls_dir(path, &opts);
        if (n_paths > 1 && i + 1 < n_paths) {
            if (write_all(STDOUT_FILENO, "\n", 1) != 0) {
                eprintf("ls: stdout: %s\n", strerror(errno));
                return 1;
            }
        }
    }
    return failed ? 1 : 0;
}
