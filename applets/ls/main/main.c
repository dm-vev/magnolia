#include <dirent.h>
#include <errno.h>
#include <getopt.h>
#include <stdbool.h>
#include <stdlib.h>
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

static void mode_string(mode_t mode, char out[11])
{
    out[0] = S_ISDIR(mode) ? 'd' : S_ISCHR(mode) ? 'c' : S_ISBLK(mode) ? 'b' : '-';
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
        printf("%s\n", display);
        return 0;
    }

    struct stat st;
    if (stat(path, &st) != 0) {
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

    printf("%s %8ld %s %s\n", mode, (long)st.st_size, timebuf, display);
    return 0;
}

static int ls_dir(const char *path, const ls_opts_t *opts)
{
    struct stat st;
    if (stat(path, &st) != 0) {
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
    while ((ent = readdir(dir)) != NULL) {
        if (!opts->all && ent->d_name[0] == '.') {
            continue;
        }
        if (count == cap) {
            size_t next = cap ? cap * 2 : 32;
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
        size_t full_len = plen + (need_slash ? 1 : 0) + strlen(name) + 1;
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

    int opt;
    while ((opt = getopt(argc, argv, "ald1")) != -1) {
        switch (opt) {
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

    int n_paths = argc - optind;
    if (n_paths <= 0) {
        return ls_dir(".", &opts);
    }

    int failed = 0;
    for (int i = optind; i < argc; ++i) {
        const char *path = argv[i];
        if (path == NULL) {
            continue;
        }
        if (n_paths > 1) {
            struct stat st;
            if (stat(path, &st) == 0 && S_ISDIR(st.st_mode) && !opts.list_dirs) {
                printf("%s:\n", path);
            }
        }

        failed |= ls_dir(path, &opts);
        if (n_paths > 1 && i + 1 < argc) {
            printf("\n");
        }
    }
    return failed ? 1 : 0;
}
