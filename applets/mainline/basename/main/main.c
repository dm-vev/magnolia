#include <errno.h>
#include <getopt.h>
#include <stdbool.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
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

static void usage(void)
{
    eprintf("usage: basename [-a] [-s suffix] string [...]\n");
}

static void compute_basename(const char *path, const char *suffix, size_t suffix_len,
                             const char **out_base, size_t *out_len)
{
    size_t len = path ? strlen(path) : 0;
    if (len == 0) {
        *out_base = ".";
        *out_len = 1;
        return;
    }

    size_t end = len;
    /* BSD basename: drop trailing slashes, keeping a single "/" for all-slash paths. */
    while (end > 0 && path[end - 1] == '/') {
        end--;
    }
    if (end == 0) {
        *out_base = "/";
        *out_len = 1;
        return;
    }

    size_t start = end;
    while (start > 0 && path[start - 1] != '/') {
        start--;
    }

    *out_base = path + start;
    *out_len = end - start;

    /* Remove the suffix only when it matches and leaves at least one character. */
    if (suffix_len > 0 && *out_len > suffix_len) {
        const char *tail = (*out_base) + (*out_len - suffix_len);
        if (memcmp(tail, suffix, suffix_len) == 0) {
            *out_len -= suffix_len;
        }
    }
}

static int output_basename(const char *path, const char *suffix, size_t suffix_len)
{
    const char *base = NULL;
    size_t base_len = 0;
    compute_basename(path, suffix, suffix_len, &base, &base_len);
    if (write_all(STDOUT_FILENO, base, base_len) != 0 ||
        write_all(STDOUT_FILENO, "\n", 1) != 0) {
        eprintf("basename: stdout: %s\n", strerror(errno));
        return 1;
    }
    return 0;
}

int main(int argc, char **argv)
{
    opterr = 0;
    bool all = false;
    const char *suffix = NULL;
    bool suffix_set = false;

    int ch;
    while ((ch = getopt(argc, argv, ":as:")) != -1) {
        switch (ch) {
        case 'a':
            all = true;
            break;
        case 's':
            suffix = optarg;
            suffix_set = true;
            break;
        case ':':
            eprintf("basename: option requires an argument -- %c\n", optopt);
            usage();
            return 1;
        default:
            eprintf("basename: illegal option -- %c\n", optopt);
            usage();
            return 1;
        }
    }

    int remaining = argc - optind;
    if (remaining <= 0) {
        usage();
        return 1;
    }

    const char *effective_suffix = suffix;
    if (!all) {
        if (remaining > 2) {
            usage();
            return 1;
        }
        if (remaining == 2) {
            if (suffix_set) {
                usage();
                return 1;
            }
            effective_suffix = argv[optind + 1];
        }
        size_t suffix_len = effective_suffix ? strlen(effective_suffix) : 0;
        return output_basename(argv[optind], effective_suffix, suffix_len);
    }

    size_t suffix_len = effective_suffix ? strlen(effective_suffix) : 0;
    int failed = 0;
    for (int i = optind; i < argc; ++i) {
        if (output_basename(argv[i], effective_suffix, suffix_len) != 0) {
            failed = 1;
            break;
        }
    }
    return failed;
}
