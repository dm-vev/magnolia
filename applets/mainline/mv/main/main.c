/*
 * BSD reference: FreeBSD mv(1).
 * Behavior notes:
 * - This implementation uses copy+remove because rename(2) is unavailable.
 * - Symlinks are moved as symlinks (targets are not dereferenced).
 * - Directory walks avoid following symlink loops.
 */
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <stdint.h>
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

static int lstat_compat(const char *path, struct stat *st)
{
#ifdef ESP_PLATFORM
    return stat(path, st);
#else
    return lstat(path, st);
#endif
}

static int write_all(int fd, const void *buf, size_t len)
{
    const unsigned char *p = (const unsigned char *)buf;
    size_t off = 0;
    while (off < len) {
        ssize_t w = write(fd, p + off, len - off);
        if (w < 0 && errno == EINTR) {
            continue;
        }
        if (w <= 0) {
            if (w == 0) {
                errno = EIO;
            }
            return -1;
        }
        off += (size_t)w;
    }
    return 0;
}

static const char *path_basename(const char *path)
{
    if (!path) {
        return "";
    }
    const char *last = strrchr(path, '/');
    return last ? last + 1 : path;
}

static char *join_path(const char *dir, const char *name)
{
    size_t dlen = strlen(dir);
    size_t nlen = strlen(name);
    bool need_slash = (dlen > 0 && dir[dlen - 1] != '/');
    size_t slash = need_slash ? 1 : 0;
    if (dlen > SIZE_MAX - nlen - slash - 1) {
        errno = ENAMETOOLONG;
        return NULL;
    }
    size_t total = dlen + slash + nlen + 1;
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

static int split_parent_base(const char *path, char **out_parent, char **out_base)
{
    if (path == NULL || path[0] == '\0') {
        errno = EINVAL;
        return -1;
    }

    char *dup = strdup(path);
    if (dup == NULL) {
        errno = ENOMEM;
        return -1;
    }

    size_t len = strlen(dup);
    while (len > 1 && dup[len - 1] == '/') {
        dup[--len] = '\0';
    }

    char *slash = strrchr(dup, '/');
    char *parent = NULL;
    char *base = NULL;

    if (slash == NULL) {
        parent = strdup(".");
        base = strdup(dup);
    } else if (slash == dup) {
        parent = strdup("/");
        base = strdup(dup + 1);
    } else {
        *slash = '\0';
        parent = strdup(dup);
        base = strdup(slash + 1);
    }

    free(dup);

    if (parent == NULL || base == NULL) {
        free(parent);
        free(base);
        errno = ENOMEM;
        return -1;
    }

    *out_parent = parent;
    *out_base = base;
    return 0;
}

static char *resolve_missing_path(const char *path)
{
    char *parent = NULL;
    char *base = NULL;
    if (split_parent_base(path, &parent, &base) != 0) {
        return NULL;
    }

    char *parent_real = realpath(parent, NULL);
    int saved_errno = errno;
    free(parent);
    if (parent_real == NULL) {
        free(base);
        errno = saved_errno;
        return NULL;
    }

    if (base[0] == '\0') {
        free(base);
        return parent_real;
    }

    size_t plen = strlen(parent_real);
    size_t blen = strlen(base);
    bool need_slash = (plen > 0 && parent_real[plen - 1] != '/');
    if (blen > SIZE_MAX - plen - (need_slash ? 1 : 0) - 1) {
        free(parent_real);
        free(base);
        errno = ENAMETOOLONG;
        return NULL;
    }
    size_t total = plen + (need_slash ? 1 : 0) + blen + 1;
    char *full = (char *)malloc(total);
    if (full == NULL) {
        free(parent_real);
        free(base);
        errno = ENOMEM;
        return NULL;
    }

    memcpy(full, parent_real, plen);
    size_t off = plen;
    if (need_slash) {
        full[off++] = '/';
    }
    memcpy(full + off, base, blen);
    full[off + blen] = '\0';

    free(parent_real);
    free(base);
    return full;
}

static bool path_is_descendant(const char *ancestor, const char *path)
{
    if (ancestor == NULL || path == NULL) {
        return false;
    }

    size_t alen = strlen(ancestor);
    if (alen == 0) {
        return false;
    }

    if (strcmp(ancestor, "/") == 0) {
        return path[0] == '/';
    }

    if (strncmp(path, ancestor, alen) != 0) {
        return false;
    }
    if (path[alen] == '\0') {
        return true;
    }
    return path[alen] == '/';
}

static bool is_descendant_dir(const char *src, const char *dst)
{
    if (src == NULL || dst == NULL) {
        return false;
    }

    char *src_real = realpath(src, NULL);
    if (src_real == NULL) {
        return false;
    }

    char *dst_real = realpath(dst, NULL);
    if (dst_real == NULL) {
        if (errno == ENOENT || errno == ENOTDIR) {
            dst_real = resolve_missing_path(dst);
        }
    }
    if (dst_real == NULL) {
        free(src_real);
        return false;
    }

    bool result = path_is_descendant(src_real, dst_real);
    free(src_real);
    free(dst_real);
    return result;
}

static int copy_file(const char *src, const char *dst, bool force)
{
    if (force) {
        (void)unlink(dst);
    }
    int in = open(src, O_RDONLY);
    if (in < 0) {
        return -1;
    }
    int out = open(dst, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (out < 0) {
        (void)close(in);
        return -1;
    }

    char buf[1024];
    while (1) {
        ssize_t r = read(in, buf, sizeof(buf));
        if (r < 0 && errno == EINTR) {
            continue;
        }
        if (r < 0) {
            (void)close(in);
            (void)close(out);
            return -1;
        }
        if (r == 0) {
            break;
        }
        if (write_all(out, buf, (size_t)r) != 0) {
            (void)close(in);
            (void)close(out);
            return -1;
        }
    }
    if (close(in) != 0) {
        (void)close(out);
        return -1;
    }
    if (close(out) != 0) {
        return -1;
    }
    return 0;
}

static int ensure_dir(const char *path)
{
    struct stat st;
    if (stat(path, &st) == 0) {
        if (S_ISDIR(st.st_mode)) {
            return 0;
        }
        errno = ENOTDIR;
        return -1;
    }
    if (mkdir(path, 0777) != 0) {
        return -1;
    }
    return 0;
}

static int rm_tree(const char *path)
{
    struct stat st;
    if (lstat_compat(path, &st) != 0) {
        return -1;
    }
    if (S_ISDIR(st.st_mode)) {
        DIR *dir = opendir(path);
        if (!dir) {
            return -1;
        }
        struct dirent *ent;
        while (1) {
            errno = 0;
            ent = readdir(dir);
            if (ent == NULL) {
                break;
            }
            if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) {
                continue;
            }
            char *child = join_path(path, ent->d_name);
            if (!child) {
                (void)closedir(dir);
                return -1;
            }
            if (rm_tree(child) != 0) {
                free(child);
                (void)closedir(dir);
                return -1;
            }
            free(child);
        }
        if (errno != 0) {
            (void)closedir(dir);
            return -1;
        }
        (void)closedir(dir);
    }
    return remove(path);
}

static int read_symlink_target(const char *src, char **out_target)
{
    struct stat st;
    if (lstat_compat(src, &st) != 0) {
        return -1;
    }
    if (st.st_size < 0) {
        errno = EIO;
        return -1;
    }
    size_t cap = 128;
    if (st.st_size > 0) {
        if ((unsigned long long)st.st_size >= SIZE_MAX) {
            errno = ENAMETOOLONG;
            return -1;
        }
        cap = (size_t)st.st_size + 1;
        if (cap == 0) {
            errno = ENAMETOOLONG;
            return -1;
        }
    }
    for (;;) {
        char *buf = (char *)malloc(cap);
        if (!buf) {
            errno = ENOMEM;
            return -1;
        }
        ssize_t len = readlink(src, buf, cap - 1);
        if (len < 0) {
            int err = errno;
            free(buf);
            errno = err;
            return -1;
        }
        if ((size_t)len < cap - 1) {
            buf[len] = '\0';
            *out_target = buf;
            return 0;
        }
        free(buf);
        if (cap > SIZE_MAX / 2) {
            errno = ENAMETOOLONG;
            return -1;
        }
        cap *= 2;
    }
}

static int copy_symlink(const char *src, const char *dst, bool force)
{
    if (force) {
        (void)unlink(dst);
    }
    char *target = NULL;
    if (read_symlink_target(src, &target) != 0) {
        return -1;
    }
    if (symlink(target, dst) != 0) {
        int err = errno;
        free(target);
        errno = err;
        return -1;
    }
    free(target);
    return 0;
}

static int mv_tree(const char *src, const char *dst, bool force);

static int mv_entry(const char *src, const char *dst, bool force)
{
    struct stat st;
    if (lstat_compat(src, &st) != 0) {
        return -1;
    }
    if (!dst) {
        errno = EINVAL;
        return -1;
    }
    struct stat dst_st;
    if (lstat_compat(dst, &dst_st) == 0) {
        if (st.st_dev == dst_st.st_dev && st.st_ino == dst_st.st_ino) {
            /* Same file: mimic rename(2) no-op to avoid data loss. */
            return 0;
        }
    } else if (errno != ENOENT) {
        return -1;
    }
    if (S_ISLNK(st.st_mode)) {
        if (copy_symlink(src, dst, force) != 0) {
            return -1;
        }
        if (unlink(src) != 0) {
            return -1;
        }
        return 0;
    }
    if (S_ISDIR(st.st_mode)) {
        return mv_tree(src, dst, force);
    }
    if (copy_file(src, dst, force) != 0) {
        return -1;
    }
    if (remove(src) != 0) {
        return -1;
    }
    return 0;
}

static int mv_tree(const char *src, const char *dst, bool force)
{
    /* Prevent infinite recursion when the destination is inside the source tree. */
    if (is_descendant_dir(src, dst)) {
        errno = EINVAL;
        return -1;
    }
    if (ensure_dir(dst) != 0) {
        return -1;
    }
    DIR *dir = opendir(src);
    if (!dir) {
        return -1;
    }
    int failed = 0;
    struct dirent *ent;
    while (1) {
        errno = 0;
        ent = readdir(dir);
        if (ent == NULL) {
            break;
        }
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) {
            continue;
        }
        char *s = join_path(src, ent->d_name);
        char *d = join_path(dst, ent->d_name);
        if (!s || !d) {
            failed = 1;
            free(s);
            free(d);
            continue;
        }
        if (mv_entry(s, d, force) != 0) {
            failed = 1;
        }
        free(s);
        free(d);
    }
    if (errno != 0) {
        failed = 1;
    }
    (void)closedir(dir);
    if (!failed) {
        if (rm_tree(src) != 0) {
            return -1;
        }
    }
    return failed ? -1 : 0;
}

static void print_help(void)
{
    printf("usage: mv [OPTION]... SOURCE... DEST\n");
    printf("  -f           do not prompt before overwrite\n");
    printf("      --help   display this help and exit\n");
    printf("      --version output version information and exit\n");
    printf("note: rename(2) is not available yet; mv uses copy+remove.\n");
}

static void print_version(void)
{
    printf("mv (%s)\n", g_version);
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

    bool force = false;
    int opt;
    while ((opt = getopt(argc, argv, "f")) != -1) {
        switch (opt) {
        case 'f':
            force = true;
            break;
        default:
            eprintf("usage: mv [-f] SOURCE... DEST\n");
            return 1;
        }
    }

    if (argc - optind < 2) {
        eprintf("mv: missing file operand\n");
        return 1;
    }
    int nsrc = argc - optind - 1;
    const char *dst = argv[argc - 1];

    struct stat dst_st;
    bool dst_is_dir = (dst && stat(dst, &dst_st) == 0 && S_ISDIR(dst_st.st_mode));
    if (nsrc > 1 && !dst_is_dir) {
        eprintf("mv: target '%s' is not a directory\n", dst ? dst : "");
        return 1;
    }

    int failed = 0;
    for (int i = optind; i < optind + nsrc; ++i) {
        const char *src = argv[i];
        if (!src) {
            continue;
        }
        char *out = NULL;
        const char *final_dst = dst;
        if (dst_is_dir) {
            out = join_path(dst, path_basename(src));
            if (!out) {
                eprintf("mv: %s -> %s: %s\n", src, dst ? dst : "", strerror(errno));
                failed = 1;
                continue;
            }
            final_dst = out;
        }
        if (mv_entry(src, final_dst, force) != 0) {
            eprintf("mv: %s -> %s: %s\n", src, final_dst ? final_dst : "", strerror(errno));
            failed = 1;
        }
        free(out);
    }
    return failed ? 1 : 0;
}
