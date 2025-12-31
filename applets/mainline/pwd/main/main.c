#include <errno.h>
#include <getopt.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdio.h>
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

static int print_path_line(const char *path)
{
    /* Emit output via write(2) to catch stdout errors reliably. */
    size_t len = strlen(path);
    if ((len && write_all(STDOUT_FILENO, path, len) != 0) ||
        write_all(STDOUT_FILENO, "\n", 1) != 0) {
        return -1;
    }
    return 0;
}

static bool pwd_matches_current(const char *pwd)
{
    /* Only trust $PWD when it really refers to the current directory. */
    if (pwd == NULL || pwd[0] != '/') {
        return false;
    }

    struct stat pwd_st;
    struct stat dot_st;
    if (stat(pwd, &pwd_st) != 0) {
        return false;
    }
    if (stat(".", &dot_st) != 0) {
        return false;
    }
    return (pwd_st.st_dev == dot_st.st_dev) && (pwd_st.st_ino == dot_st.st_ino);
}

static char *getcwd_alloc(void)
{
    /* Grow the buffer to handle arbitrarily long paths without PATH_MAX. */
    size_t size = 256;
    for (;;) {
        char *buf = (char *)malloc(size);
        if (buf == NULL) {
            return NULL;
        }
        if (getcwd(buf, size) != NULL) {
            return buf;
        }
        int err = errno;
        free(buf);
        if (err != ERANGE) {
            errno = err;
            return NULL;
        }
        if (size > SIZE_MAX / 2) {
            errno = ERANGE;
            return NULL;
        }
        size *= 2;
    }
}

int main(int argc, char **argv)
{
    bool logical = true;

    int opt;
    while ((opt = getopt(argc, argv, "LP")) != -1) {
        switch (opt) {
        case 'L':
            logical = true;
            break;
        case 'P':
            logical = false;
            break;
        default:
            eprintf("usage: pwd [-L|-P]\n");
            return 1;
        }
    }
    if (optind < argc) {
        eprintf("pwd: too many arguments\n");
        return 1;
    }

    if (logical) {
        const char *pwd = getenv("PWD");
        if (pwd_matches_current(pwd)) {
            if (print_path_line(pwd) != 0) {
                eprintf("pwd: stdout: %s\n", strerror(errno));
                return 1;
            }
            return 0;
        }
    }
    char *cwd = getcwd_alloc();
    if (cwd == NULL) {
        eprintf("pwd: %s\n", strerror(errno));
        return 1;
    }
    if (print_path_line(cwd) != 0) {
        eprintf("pwd: stdout: %s\n", strerror(errno));
        free(cwd);
        return 1;
    }
    free(cwd);
    return 0;
}
