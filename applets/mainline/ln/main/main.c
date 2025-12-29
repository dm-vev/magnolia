#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <stdbool.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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

static bool confirm_overwrite(const char *path)
{
    char prompt[256];
    int n = snprintf(prompt, sizeof(prompt), "ln: replace '%s'? ", path);
    if (n > 0) {
        size_t len = (size_t)n;
        if (len >= sizeof(prompt)) {
            len = sizeof(prompt) - 1;
        }
        (void)write_all(STDERR_FILENO, prompt, len);
    }

    char buf[16];
    ssize_t r = read(STDIN_FILENO, buf, sizeof(buf));
    if (r <= 0) {
        return false;
    }
    return buf[0] == 'y' || buf[0] == 'Y';
}

static const char *path_basename(const char *path, size_t *out_len)
{
    size_t len = path ? strlen(path) : 0;
    if (len == 0) {
        *out_len = 0;
        return "";
    }
    size_t end = len;
    while (end > 0 && path[end - 1] == '/') {
        end--;
    }
    if (end == 0) {
        *out_len = 1;
        return "/";
    }
    size_t start = end;
    while (start > 0 && path[start - 1] != '/') {
        start--;
    }
    *out_len = end - start;
    return path + start;
}

static char *join_path_len(const char *dir, const char *name, size_t name_len)
{
    if (name_len == 0) {
        errno = EINVAL;
        return NULL;
    }
    for (size_t i = 0; i < name_len; ++i) {
        if (name[i] == '/') {
            errno = EINVAL;
            return NULL;
        }
    }

    size_t dlen = strlen(dir);
    bool need_slash = (dlen > 0 && dir[dlen - 1] != '/');
    if (dlen > SIZE_MAX - name_len - 2) {
        errno = EOVERFLOW;
        return NULL;
    }
    size_t total = dlen + (need_slash ? 1 : 0) + name_len + 1;
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
    memcpy(out + off, name, name_len);
    out[off + name_len] = '\0';
    return out;
}

static char *join_path(const char *dir, const char *name)
{
    return join_path_len(dir, name, strlen(name));
}

static char *make_target_path(const char *dir, const char *src)
{
    size_t base_len = 0;
    const char *base = path_basename(src, &base_len);
    if (base_len == 0 || (base_len == 1 && base[0] == '/')) {
        errno = EINVAL;
        return NULL;
    }
    return join_path_len(dir, base, base_len);
}

static int lstat_compat(const char *path, struct stat *st)
{
#ifdef ESP_PLATFORM
    return stat(path, st);
#else
    return lstat(path, st);
#endif
}

static int stat_path(const char *path, bool follow, struct stat *out)
{
    struct stat st;
    if (lstat_compat(path, &st) != 0) {
        return -1;
    }
    if (follow && S_ISLNK(st.st_mode)) {
        if (stat(path, out) != 0) {
            return -1;
        }
        return 0;
    }
    *out = st;
    return 0;
}

static int rm_tree(const char *path)
{
    struct stat st;
    if (lstat_compat(path, &st) != 0) {
        return -1;
    }
    if (!S_ISDIR(st.st_mode)) {
        return unlink(path);
    }

    DIR *dir = opendir(path);
    if (!dir) {
        return -1;
    }

    int rc = 0;
    struct dirent *ent;
    while ((ent = readdir(dir)) != NULL) {
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) {
            continue;
        }
        char *child = join_path(path, ent->d_name);
        if (!child) {
            rc = -1;
            break;
        }
        if (rm_tree(child) != 0) {
            free(child);
            rc = -1;
            break;
        }
        free(child);
    }
    (void)closedir(dir);
    if (rc != 0) {
        return -1;
    }
    return rmdir(path);
}

static int ensure_target_ready(const char *dst,
                               bool force,
                               bool interactive,
                               bool force_dir,
                               bool *skipped)
{
    struct stat st;
    if (lstat_compat(dst, &st) != 0) {
        if (errno == ENOENT) {
            return 0;
        }
        return -1;
    }

    if (interactive) {
        if (!confirm_overwrite(dst)) {
            if (skipped) {
                *skipped = true;
            }
            return 0;
        }
    }

    if (S_ISDIR(st.st_mode)) {
        if (!force_dir) {
            errno = EISDIR;
            return -1;
        }
        /* TODO: Confirm BSD -F semantics for non-empty directories. */
        return rm_tree(dst);
    }

    if (!force && !interactive) {
        errno = EEXIST;
        return -1;
    }
    return unlink(dst);
}

static int link_hard(const char *src, const char *dst, bool follow_source)
{
#if defined(AT_FDCWD)
    if (!follow_source) {
        return linkat(AT_FDCWD, src, AT_FDCWD, dst, 0);
    }
#if defined(AT_SYMLINK_FOLLOW)
    return linkat(AT_FDCWD, src, AT_FDCWD, dst, AT_SYMLINK_FOLLOW);
#else
    return link(src, dst);
#endif
#else
    if (!follow_source) {
        /* TODO: use linkat when available so -P can link a symlink itself. */
    }
    return link(src, dst);
#endif
}

static int create_link(const char *src, const char *dst, bool symlink_mode, bool follow_source)
{
    if (symlink_mode) {
        return symlink(src, dst);
    }
    return link_hard(src, dst, follow_source);
}

static int print_verbose(const char *dst, const char *src)
{
    if (write_all(STDOUT_FILENO, dst, strlen(dst)) != 0 ||
        write_all(STDOUT_FILENO, " -> ", 4) != 0 ||
        write_all(STDOUT_FILENO, src, strlen(src)) != 0 ||
        write_all(STDOUT_FILENO, "\n", 1) != 0) {
        return -1;
    }
    return 0;
}

static void usage(void)
{
    eprintf("usage: ln [-Ffhinsv] [-L | -P] source [target]\n");
    eprintf("       ln [-Ffhinsv] [-L | -P] source ... directory\n");
}

int main(int argc, char **argv)
{
    opterr = 0;
    optind = 1;
#ifdef optreset
    optreset = 1;
#endif

    bool force = false;
    bool interactive = false;
    bool symlink_mode = false;
    bool no_follow_target = false;
    /* TODO: Confirm BSD default for -L/-P source symlink handling. */
    bool follow_source = true;
    bool verbose = false;
    bool force_dir = false;

    int ch;
    while ((ch = getopt(argc, argv, "FfhinsvLP")) != -1) {
        switch (ch) {
        case 'F':
            force_dir = true;
            break;
        case 'f':
            force = true;
            interactive = false;
            break;
        case 'h':
        case 'n':
            no_follow_target = true;
            break;
        case 'i':
            interactive = true;
            force = false;
            break;
        case 's':
            symlink_mode = true;
            break;
        case 'v':
            verbose = true;
            break;
        case 'L':
            follow_source = true;
            break;
        case 'P':
            follow_source = false;
            break;
        default:
            eprintf("ln: illegal option -- %c\n", optopt);
            usage();
            return 1;
        }
    }

    int n_operands = argc - optind;
    if (n_operands < 1) {
        usage();
        return 1;
    }

    bool dst_specified = n_operands > 1;
    const char *dst_arg = dst_specified ? argv[optind + n_operands - 1] : ".";
    int nsrc = dst_specified ? n_operands - 1 : 1;
    bool dst_must_be_dir = (nsrc > 1) || !dst_specified;

    struct stat dst_st;
    bool dst_exists = false;
    bool dst_is_dir = false;
    if (stat_path(dst_arg, !no_follow_target, &dst_st) == 0) {
        dst_exists = true;
        dst_is_dir = S_ISDIR(dst_st.st_mode);
    } else if (errno != ENOENT) {
        eprintf("ln: %s: %s\n", dst_arg, strerror(errno));
        return 1;
    }

    if (dst_must_be_dir) {
        if (!dst_exists) {
            eprintf("ln: %s: %s\n", dst_arg, strerror(errno));
            return 1;
        }
        if (!dst_is_dir) {
            eprintf("ln: %s: not a directory\n", dst_arg);
            return 1;
        }
    }

    bool treat_dst_as_dir = dst_must_be_dir || (dst_exists && dst_is_dir && !force_dir);
    int failed = 0;

    for (int i = 0; i < nsrc; ++i) {
        const char *src = argv[optind + i];
        if (!src) {
            continue;
        }

        char *target = treat_dst_as_dir ? make_target_path(dst_arg, src) : strdup(dst_arg);
        if (!target) {
            eprintf("ln: %s -> %s: %s\n", src, dst_arg, strerror(errno));
            failed = 1;
            continue;
        }

        if (force || interactive || force_dir) {
            bool skipped = false;
            if (ensure_target_ready(target, force, interactive, force_dir, &skipped) != 0) {
                eprintf("ln: %s -> %s: %s\n", src, target, strerror(errno));
                failed = 1;
                free(target);
                continue;
            }
            if (skipped) {
                free(target);
                continue;
            }
        }

        if (create_link(src, target, symlink_mode, follow_source) != 0) {
            eprintf("ln: %s -> %s: %s\n", src, target, strerror(errno));
            failed = 1;
            free(target);
            continue;
        }

        if (verbose) {
            if (print_verbose(target, src) != 0) {
                eprintf("ln: stdout: %s\n", strerror(errno));
                failed = 1;
                free(target);
                continue;
            }
        }

        free(target);
    }

    return failed ? 1 : 0;
}
