#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <signal.h>
#include <stdbool.h>
#include <stdarg.h>
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

static ssize_t read_retry(int fd, void *buf, size_t len)
{
    while (1) {
        ssize_t r = read(fd, buf, len);
        if (r < 0 && errno == EINTR) {
            continue;
        }
        return r;
    }
}

typedef struct {
    int fd;
    const char *path;
    bool active;
} output_t;

int main(int argc, char **argv)
{
    bool append = false;
    bool ignore_int = false;
    int opt;
    opterr = 0;
    while ((opt = getopt(argc, argv, "ai")) != -1) {
        switch (opt) {
        case 'a':
            append = true;
            break;
        case 'i':
            ignore_int = true;
            break;
        default:
            eprintf("usage: tee [-ai] [file ...]\n");
            return 1;
        }
    }

    if (ignore_int) {
        (void)signal(SIGINT, SIG_IGN);
    }

    size_t out_cap = 0;
    if (optind < argc) {
        out_cap = (size_t)(argc - optind);
    }
    output_t *outs = NULL;
    if (out_cap > 0) {
        outs = (output_t *)calloc(out_cap, sizeof(*outs));
        if (!outs) {
            eprintf("tee: %s\n", strerror(errno));
            return 1;
        }
    }

    size_t out_count = 0;
    int failed = 0;
    for (int i = optind; i < argc; ++i) {
        const char *path = argv[i];
        if (!path) {
            continue;
        }
        if (strcmp(path, "-") == 0) {
            continue;
        }
        int flags = O_WRONLY | O_CREAT | (append ? O_APPEND : O_TRUNC);
        int fd = open(path, flags, 0666);
        if (fd < 0) {
            eprintf("tee: %s: %s\n", path, strerror(errno));
            failed = 1;
            continue;
        }
        outs[out_count].fd = fd;
        outs[out_count].path = path;
        outs[out_count].active = true;
        out_count++;
    }

    char buf[512];
    while (1) {
        ssize_t r = read_retry(STDIN_FILENO, buf, sizeof(buf));
        if (r < 0) {
            eprintf("tee: read: %s\n", strerror(errno));
            failed = 1;
            break;
        }
        if (r == 0) {
            break;
        }
        if (write_all(STDOUT_FILENO, buf, (size_t)r) != 0) {
            eprintf("tee: stdout: %s\n", strerror(errno));
            failed = 1;
            break;
        }
        for (size_t i = 0; i < out_count; ++i) {
            if (!outs[i].active) {
                continue;
            }
            if (write_all(outs[i].fd, buf, (size_t)r) != 0) {
                eprintf("tee: %s: %s\n", outs[i].path ? outs[i].path : "file", strerror(errno));
                failed = 1;
                (void)close(outs[i].fd);
                outs[i].active = false;
            }
        }
    }

    for (size_t i = 0; i < out_count; ++i) {
        if (outs[i].active) {
            (void)close(outs[i].fd);
        }
    }
    free(outs);
    return failed ? 1 : 0;
}
