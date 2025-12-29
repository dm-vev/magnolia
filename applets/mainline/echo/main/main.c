#include <errno.h>
#include <stdbool.h>
#include <stdarg.h>
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

enum echo_escape_result {
    ECHO_ESCAPE_OK = 0,
    ECHO_ESCAPE_STOP = 1,
    ECHO_ESCAPE_ERROR = -1,
};

static int echo_write_escaped(const char *s)
{
    for (size_t i = 0; s[i] != '\0'; ++i) {
        char out = s[i];
        if (out == '\\') {
            char next = s[i + 1];
            if (next == '\0') {
                break;
            }
            i++;

            switch (next) {
            case '\\':
                out = '\\';
                break;
            case 'a':
                out = '\a';
                break;
            case 'b':
                out = '\b';
                break;
            case 'f':
                out = '\f';
                break;
            case 'n':
                out = '\n';
                break;
            case 'r':
                out = '\r';
                break;
            case 't':
                out = '\t';
                break;
            case 'v':
                out = '\v';
                break;
            case 'c':
                // \c stops output and suppresses the trailing newline.
                return ECHO_ESCAPE_STOP;
            case '0': {
                unsigned value = 0;
                unsigned digits = 0;
                while (digits < 3) {
                    char d = s[i + 1];
                    if (d < '0' || d > '7') {
                        break;
                    }
                    i++;
                    value = (value * 8u) + (unsigned)(d - '0');
                    digits++;
                }
                out = (char)(value & 0xff);
                break;
            }
            default:
                if (write_all(STDOUT_FILENO, "\\", 1) != 0) {
                    return ECHO_ESCAPE_ERROR;
                }
                out = next;
                break;
            }
        }

        if (write_all(STDOUT_FILENO, &out, 1) != 0) {
            return ECHO_ESCAPE_ERROR;
        }
    }
    return ECHO_ESCAPE_OK;
}

int main(int argc, char **argv)
{
    bool newline = true;

    int i = 1;
    // BSD echo only recognizes a single leading -n.
    if (i < argc && argv[i] != NULL && strcmp(argv[i], "-n") == 0) {
        newline = false;
        i++;
    }

    bool first = true;
    for (; i < argc; ++i) {
        const char *arg = argv[i] ? argv[i] : "";
        if (!first) {
            if (write_all(STDOUT_FILENO, " ", 1) != 0) {
                eprintf("echo: write: %s\n", strerror(errno));
                return 1;
            }
        }
        first = false;

        int rc = echo_write_escaped(arg);
        if (rc == ECHO_ESCAPE_ERROR) {
            eprintf("echo: write: %s\n", strerror(errno));
            return 1;
        }
        if (rc == ECHO_ESCAPE_STOP) {
            newline = false;
            return 0;
        }
    }

    if (newline) {
        if (write_all(STDOUT_FILENO, "\n", 1) != 0) {
            eprintf("echo: write: %s\n", strerror(errno));
            return 1;
        }
    }
    return 0;
}
