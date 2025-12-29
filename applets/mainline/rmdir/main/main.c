#include <errno.h>
#include <getopt.h>
#include <stdbool.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
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

static void usage(void)
{
    eprintf("usage: rmdir [-p] dir ...\n");
}

static size_t parent_length(const char *path, size_t len)
{
    /* Compute a removable parent length; 0 means stop at "." or root. */
    while (len > 1 && path[len - 1] == '/') {
        len--;
    }
    if (len == 1 && path[0] == '/') {
        return 0;
    }
    size_t i = len;
    while (i > 0 && path[i - 1] != '/') {
        i--;
    }
    if (i == 0) {
        return 0;
    }
    size_t parent_len = i - 1;
    while (parent_len > 1 && path[parent_len - 1] == '/') {
        parent_len--;
    }
    if (parent_len == 0) {
        parent_len = 1;
    }
    return parent_len;
}

static int remove_with_parents(const char *path)
{
    if (rmdir(path) != 0) {
        eprintf("rmdir: %s: %s\n", path, strerror(errno));
        return 1;
    }

    /* Walk up the path, removing empty parents until root or ".". */
    char *buf = strdup(path);
    if (!buf) {
        eprintf("rmdir: %s: %s\n", path, strerror(ENOMEM));
        return 1;
    }

    size_t len = strlen(buf);
    while (1) {
        size_t next_len = parent_length(buf, len);
        if (next_len == 0) {
            break;
        }
        buf[next_len] = '\0';
        if ((next_len == 1 && buf[0] == '/') || strcmp(buf, ".") == 0) {
            break;
        }
        if (rmdir(buf) != 0) {
            eprintf("rmdir: %s: %s\n", buf, strerror(errno));
            free(buf);
            return 1;
        }
        len = next_len;
    }

    free(buf);
    return 0;
}

int main(int argc, char **argv)
{
    opterr = 0;
    bool parents = false;
    int ch;
    while ((ch = getopt(argc, argv, "p")) != -1) {
        switch (ch) {
        case 'p':
            parents = true;
            break;
        default:
            eprintf("rmdir: illegal option -- %c\n", optopt);
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
        if (!path) {
            continue;
        }
        int rc = parents ? remove_with_parents(path) : (rmdir(path) != 0);
        if (!parents && rc) {
            eprintf("rmdir: %s: %s\n", path, strerror(errno));
            failed = 1;
        } else if (parents && rc) {
            failed = 1;
        }
    }

    return failed ? 1 : 0;
}
