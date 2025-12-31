#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

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

static int oprintf(const char *fmt, ...)
{
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (n <= 0) {
        return 0;
    }
    size_t len = (size_t)n;
    if (len >= sizeof(buf)) {
        len = sizeof(buf) - 1;
    }
    return write_all(STDOUT_FILENO, buf, len);
}

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
    (void)write_all(STDERR_FILENO, buf, len);
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

static int size_mul(size_t a, size_t b, size_t *out)
{
    if (a != 0 && b > SIZE_MAX / a) {
        return -1;
    }
    *out = a * b;
    return 0;
}

static int size_add(size_t a, size_t b, size_t *out)
{
    if (a > SIZE_MAX - b) {
        return -1;
    }
    *out = a + b;
    return 0;
}

static int append_fmt(char *buf, size_t cap, size_t *pos, const char *fmt, ...)
{
    if (*pos >= cap) {
        errno = EOVERFLOW;
        return -1;
    }
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf + *pos, cap - *pos, fmt, ap);
    va_end(ap);
    if (n < 0) {
        return -1;
    }
    if ((size_t)n >= cap - *pos) {
        errno = EOVERFLOW;
        return -1;
    }
    *pos += (size_t)n;
    return 0;
}

static uint64_t off_t_max(void)
{
    /* Compute the maximum representable positive off_t to avoid truncation. */
    if ((off_t)-1 > 0) {
        return (uint64_t)~(off_t)0;
    }
    size_t bits = sizeof(off_t) * CHAR_BIT;
    if (bits >= sizeof(uint64_t) * CHAR_BIT) {
        return UINT64_MAX;
    }
    return (uint64_t)((1ULL << (bits - 1)) - 1);
}

static int calc_line_capacity(int columns, int group, size_t *out)
{
    if (columns <= 0) {
        *out = 0;
        return 0;
    }
    size_t cols = (size_t)columns;
    size_t total = 0;
    size_t tmp = 0;

    /* "%08llx: " prefix is 10 bytes, then hex, spacer, ASCII, and newline. */
    if (size_add(total, 10, &total) != 0) {
        return -1;
    }
    if (size_mul(cols, 2, &tmp) != 0 || size_add(total, tmp, &total) != 0) {
        return -1;
    }
    if (group > 0) {
        if (size_add(total, cols / (size_t)group, &total) != 0) {
            return -1;
        }
    }
    if (size_add(total, 1, &total) != 0) {
        return -1;
    }
    if (size_add(total, cols, &total) != 0) {
        return -1;
    }
    if (size_add(total, 1, &total) != 0) {
        return -1;
    }
    if (size_add(total, 1, &total) != 0) {
        return -1;
    }
    *out = total;
    return 0;
}

static int parse_int_silent(const char *s, int fallback)
{
    /* Avoid atoi overflow UB; fall back on invalid or out-of-range input. */
    if (s == NULL || *s == '\0') {
        return fallback;
    }
    errno = 0;
    char *end = NULL;
    long value = strtol(s, &end, 10);
    if (errno != 0 || end == s || *end != '\0' || value < INT_MIN || value > INT_MAX) {
        return fallback;
    }
    return (int)value;
}

static int parse_token(const char *s, char **end_out, uint64_t *value)
{
    if (s == NULL || *s == '\0') {
        return -1;
    }
    errno = 0;
    char *end = NULL;
    unsigned long long raw = strtoull(s, &end, 0);
    if (end == s || errno != 0) {
        return -1;
    }
    uint64_t mult = 1;
    if (*end) {
        switch (*end) {
        case 'b':
            mult = 512ULL;
            end++;
            break;
        case 'k':
        case 'K':
            mult = 1024ULL;
            end++;
            break;
        case 'm':
        case 'M':
            mult = 1024ULL * 1024ULL;
            end++;
            break;
        case 'g':
        case 'G':
            mult = 1024ULL * 1024ULL * 1024ULL;
            end++;
            break;
        default:
            break;
        }
    }
    if (mult != 1 && raw > 0 && raw > UINT64_MAX / mult) {
        return -1;
    }
    *value = (uint64_t)raw * mult;
    *end_out = end;
    return 0;
}

static int parse_size(const char *s, uint64_t *out)
{
    const char *p = s;
    uint64_t total = 1;
    while (1) {
        char *end = NULL;
        uint64_t value = 0;
        if (parse_token(p, &end, &value) != 0) {
            return -1;
        }
        if (value > 0 && total > UINT64_MAX / value) {
            return -1;
        }
        total *= value;
        if (*end == '\0') {
            *out = total;
            return 0;
        }
        if (*end == 'x' || *end == '*') {
            p = end + 1;
            continue;
        }
        return -1;
    }
}

static int skip_bytes(int fd, uint64_t *skip_left)
{
    if (*skip_left == 0) {
        return 0;
    }
    if (*skip_left <= off_t_max()) {
        off_t offset = (off_t)(*skip_left);
        if (lseek(fd, offset, SEEK_CUR) >= 0) {
            *skip_left = 0;
            return 0;
        }
        if (errno != ESPIPE && errno != EINVAL) {
            return -1;
        }
    }
    unsigned char buf[256];
    while (*skip_left > 0) {
        size_t chunk = sizeof(buf);
        if (*skip_left < chunk) {
            chunk = (size_t)*skip_left;
        }
        ssize_t r = read_retry(fd, buf, chunk);
        if (r < 0) {
            return -1;
        }
        if (r == 0) {
            *skip_left = 0;
            return 0;
        }
        *skip_left -= (uint64_t)r;
    }
    return 0;
}

static int hex_value(int c)
{
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

static int reverse_stream(int fd)
{
    char line[256];
    size_t line_len = 0;
    int half = -1;
    unsigned char outbuf[256];
    size_t out_len = 0;

    char buf[128];
    while (1) {
        ssize_t r = read_retry(fd, buf, sizeof(buf));
        if (r < 0) {
            return -1;
        }
        if (r == 0) {
            break;
        }
        for (ssize_t i = 0; i < r; ++i) {
            char c = buf[i];
            if (c == '\n' || c == '\r') {
                line[line_len] = '\0';
                char *start = line;
                char *colon = strchr(line, ':');
                if (colon && colon - line <= 8) {
                    start = colon + 1;
                }
                for (; *start; ++start) {
                    int v = hex_value((unsigned char)*start);
                    if (v < 0) {
                        continue;
                    }
                    if (half < 0) {
                        half = v;
                    } else {
                        unsigned char byte = (unsigned char)((half << 4) | v);
                        outbuf[out_len++] = byte;
                        if (out_len == sizeof(outbuf)) {
                            if (write_all(STDOUT_FILENO, outbuf, out_len) != 0) {
                                return -1;
                            }
                            out_len = 0;
                        }
                        half = -1;
                    }
                }
                line_len = 0;
                continue;
            }
            if (line_len + 1 < sizeof(line)) {
                line[line_len++] = c;
            }
        }
    }

    if (line_len > 0) {
        line[line_len] = '\0';
        char *start = line;
        char *colon = strchr(line, ':');
        if (colon && colon - line <= 8) {
            start = colon + 1;
        }
        for (; *start; ++start) {
            int v = hex_value((unsigned char)*start);
            if (v < 0) {
                continue;
            }
            if (half < 0) {
                half = v;
            } else {
                unsigned char byte = (unsigned char)((half << 4) | v);
                outbuf[out_len++] = byte;
                if (out_len == sizeof(outbuf)) {
                    if (write_all(STDOUT_FILENO, outbuf, out_len) != 0) {
                        return -1;
                    }
                    out_len = 0;
                }
                half = -1;
            }
        }
    }

    if (out_len > 0) {
        if (write_all(STDOUT_FILENO, outbuf, out_len) != 0) {
            return -1;
        }
    }
    return 0;
}

static int xxd_forward(int fd, uint64_t skip, uint64_t length, bool use_length,
                       int columns, int group, bool plain, bool upper)
{
    if (skip_bytes(fd, &skip) != 0) {
        return -1;
    }
    char *line = NULL;
    size_t line_cap = 0;
    if (!plain) {
        if (calc_line_capacity(columns, group, &line_cap) != 0) {
            errno = EOVERFLOW;
            return -1;
        }
        line = (char *)malloc(line_cap);
        if (line == NULL) {
            return -1;
        }
    }
    uint64_t offset = 0;
    unsigned char buf[256];
    while (1) {
        size_t want = (size_t)columns;
        if (use_length && length < want) {
            want = (size_t)length;
        }
        ssize_t r = read_retry(fd, buf, want);
        if (r < 0) {
            return -1;
        }
        if (r == 0) {
            break;
        }
        size_t len = (size_t)r;
        if (plain) {
            for (size_t i = 0; i < len; ++i) {
                if (oprintf(upper ? "%02X" : "%02x", buf[i]) != 0) {
                    free(line);
                    return -1;
                }
                if (i + 1 == len || ((i + 1) % (size_t)columns) == 0) {
                    if (oprintf("\n") != 0) {
                        free(line);
                        return -1;
                    }
                }
            }
        } else {
            size_t pos = 0;
            if (append_fmt(line, line_cap, &pos, "%08llx: ", (unsigned long long)offset) != 0) {
                free(line);
                return -1;
            }
            for (size_t i = 0; i < (size_t)columns; ++i) {
                if (i < len) {
                    if (append_fmt(line, line_cap, &pos,
                                   upper ? "%02X" : "%02x", buf[i]) != 0) {
                        free(line);
                        return -1;
                    }
                } else {
                    if (append_fmt(line, line_cap, &pos, "  ") != 0) {
                        free(line);
                        return -1;
                    }
                }
                if (group > 0 && ((i + 1) % (size_t)group) == 0) {
                    if (pos >= line_cap) {
                        errno = EOVERFLOW;
                        free(line);
                        return -1;
                    }
                    line[pos++] = ' ';
                }
            }
            if (pos >= line_cap) {
                errno = EOVERFLOW;
                free(line);
                return -1;
            }
            line[pos++] = ' ';
            for (size_t i = 0; i < len; ++i) {
                unsigned char b = buf[i];
                if (pos >= line_cap) {
                    errno = EOVERFLOW;
                    free(line);
                    return -1;
                }
                line[pos++] = (b >= 0x20 && b <= 0x7e) ? (char)b : '.';
            }
            if (pos >= line_cap) {
                errno = EOVERFLOW;
                free(line);
                return -1;
            }
            line[pos++] = '\n';
            if (write_all(STDOUT_FILENO, line, pos) != 0) {
                free(line);
                return -1;
            }
        }
        offset += len;
        if (use_length) {
            length -= len;
            if (length == 0) {
                break;
            }
        }
    }
    free(line);
    return 0;
}

int main(int argc, char **argv)
{
    int columns = 16;
    int group = 2;
    uint64_t length = 0;
    uint64_t skip = 0;
    bool use_length = false;
    bool plain = false;
    bool reverse = false;
    bool upper = false;

    int opt;
    while ((opt = getopt(argc, argv, "g:c:l:s:pruh")) != -1) {
        switch (opt) {
        case 'g':
            group = parse_int_silent(optarg, 0);
            if (group < 0) {
                group = 0;
            }
            break;
        case 'c':
            columns = parse_int_silent(optarg, 0);
            if (columns <= 0 || columns > 256) {
                columns = 16;
            }
            break;
        case 'l':
            if (parse_size(optarg, &length) != 0) {
                eprintf("xxd: invalid length '%s'\n", optarg);
                return 1;
            }
            use_length = true;
            break;
        case 's':
            if (parse_size(optarg, &skip) != 0) {
                eprintf("xxd: invalid offset '%s'\n", optarg);
                return 1;
            }
            break;
        case 'p':
            plain = true;
            break;
        case 'r':
            reverse = true;
            break;
        case 'u':
            upper = true;
            break;
        case 'h':
        default:
            eprintf("usage: xxd [-g n] [-c n] [-l len] [-s offset] [-p] [-r] [-u] [file]\n");
            return 1;
        }
    }

    if (plain && columns == 16) {
        columns = 30;
    }

    const char *path = NULL;
    if (optind < argc) {
        path = argv[optind];
    }

    int fd = STDIN_FILENO;
    if (path) {
        fd = open(path, O_RDONLY);
        if (fd < 0) {
            eprintf("xxd: %s: %s\n", path, strerror(errno));
            return 1;
        }
    }

    int rc;
    if (reverse) {
        rc = reverse_stream(fd);
    } else {
        rc = xxd_forward(fd, skip, length, use_length, columns, group, plain, upper);
    }

    if (path) {
        close(fd);
    }

    if (rc != 0) {
        eprintf("xxd: %s\n", strerror(errno));
        return 1;
    }
    return 0;
}
