#include <errno.h>
#include <getopt.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
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

static bool confirm_remove(const char *path)
{
    char prompt[256];
    int n = snprintf(prompt, sizeof(prompt), "rm: remove '%s'? ", path);
    if (n > 0) {
        size_t len = (size_t)n;
        if (len >= sizeof(prompt)) {
            len = sizeof(prompt) - 1;
        }
        (void)write(STDERR_FILENO, prompt, len);
    }

    char buf[16];
    ssize_t r = read(STDIN_FILENO, buf, sizeof(buf));
    if (r <= 0) {
        return false;
    }
    return (buf[0] == 'y' || buf[0] == 'Y');
}

static char *join_path(const char *dir, const char *name)
{
    size_t dlen = strlen(dir);
    size_t nlen = strlen(name);
    bool need_slash = (dlen > 0 && dir[dlen - 1] != '/');
    size_t total = dlen + (need_slash ? 1 : 0) + nlen + 1;
    char *out = (char *)malloc(total);
    if (!out) {
        errno = ENOMEM;
        return NULL;
    }
    memcpy(out, dir, dlen);
    size_t off = dlen;
    if (need_slash) {
        out[off++] = '/';
    }
    memcpy(out + off, name, nlen);
    out[off + nlen] = '\0';
    return out;
}

static int rm_path(const char *path, bool recursive, bool force, bool interactive, int *failed)
{
    if (interactive && !confirm_remove(path)) {
        return 0;
    }

    struct stat st;
    if (stat(path, &st) != 0) {
        if (force && errno == ENOENT) {
            return 0;
        }
        eprintf("rm: %s: %s\n", path, strerror(errno));
        *failed = 1;
        return -1;
    }

    if (S_ISDIR(st.st_mode)) {
        if (!recursive) {
            eprintf("rm: %s: is a directory\n", path);
            *failed = 1;
            return -1;
        }

        DIR *dir = opendir(path);
        if (!dir) {
            eprintf("rm: %s: %s\n", path, strerror(errno));
            *failed = 1;
            return -1;
        }

        struct dirent *ent;
        while ((ent = readdir(dir)) != NULL) {
            if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) {
                continue;
            }
            char *child = join_path(path, ent->d_name);
            if (!child) {
                eprintf("rm: %s/%s: %s\n", path, ent->d_name, strerror(errno));
                *failed = 1;
                continue;
            }
            (void)rm_path(child, recursive, force, interactive, failed);
            free(child);
        }
        (void)closedir(dir);
    }

    if (remove(path) != 0) {
        if (force && errno == ENOENT) {
            return 0;
        }
        eprintf("rm: %s: %s\n", path, strerror(errno));
        *failed = 1;
        return -1;
    }
    return 0;
}

int main(int argc, char **argv)
{
    bool force = false;
    bool recursive = false;
    bool interactive = false;

    int opt;
    while ((opt = getopt(argc, argv, "fRir")) != -1) {
        switch (opt) {
        case 'f':
            force = true;
            interactive = false;
            break;
        case 'r':
        case 'R':
            recursive = true;
            break;
        case 'i':
            interactive = true;
            force = false;
            break;
        default:
            eprintf("usage: rm [-f] [-i] [-r|-R] file ...\n");
            return 1;
        }
    }

    if (optind >= argc) {
        if (force) {
            return 0;
        }
        eprintf("rm: missing operand\n");
        return 1;
    }

    int failed = 0;
    for (int i = optind; i < argc; ++i) {
        const char *path = argv[i];
        if (!path) {
            continue;
        }
        (void)rm_path(path, recursive, force, interactive, &failed);
    }
    return failed ? 1 : 0;
}
