#include <errno.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* BSD reference: FreeBSD ps(1). */

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

static char *getcwd_dynamic(void)
{
    size_t size = 256;
    for (;;) {
        char *buf = (char *)malloc(size);
        if (!buf) {
            return NULL;
        }
        if (getcwd(buf, size) != NULL) {
            return buf;
        }
        free(buf);
        if (errno != ERANGE) {
            return NULL;
        }
        if (size > SIZE_MAX / 2) {
            errno = ENOMEM;
            return NULL;
        }
        size *= 2;
    }
}

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    char *cwd = getcwd_dynamic();
    char fallback[256];
    const char *cwd_out = cwd;
    if (cwd_out == NULL) {
        (void)snprintf(fallback, sizeof(fallback), "<cwd error: %s>", strerror(errno));
        cwd_out = fallback;
    }

    if (write_all(STDOUT_FILENO, "PID\tPPID\tCWD\n", strlen("PID\tPPID\tCWD\n")) != 0) {
        eprintf("ps: stdout: %s\n", strerror(errno));
        free(cwd);
        return 1;
    }

    char pid_buf[32];
    int pid_len = snprintf(pid_buf, sizeof(pid_buf), "%d", (int)getpid());
    if (pid_len < 0 || (size_t)pid_len >= sizeof(pid_buf)) {
        eprintf("ps: stdout: %s\n", strerror(EOVERFLOW));
        free(cwd);
        return 1;
    }
    char ppid_buf[32];
    int ppid_len = snprintf(ppid_buf, sizeof(ppid_buf), "%d", (int)getppid());
    if (ppid_len < 0 || (size_t)ppid_len >= sizeof(ppid_buf)) {
        eprintf("ps: stdout: %s\n", strerror(EOVERFLOW));
        free(cwd);
        return 1;
    }

    bool ok = write_all(STDOUT_FILENO, pid_buf, (size_t)pid_len) == 0
              && write_all(STDOUT_FILENO, "\t", 1) == 0
              && write_all(STDOUT_FILENO, ppid_buf, (size_t)ppid_len) == 0
              && write_all(STDOUT_FILENO, "\t", 1) == 0
              && write_all(STDOUT_FILENO, cwd_out, strlen(cwd_out)) == 0
              && write_all(STDOUT_FILENO, "\n", 1) == 0;
    if (!ok) {
        eprintf("ps: stdout: %s\n", strerror(errno));
        free(cwd);
        return 1;
    }

    free(cwd);
    return 0;
}
