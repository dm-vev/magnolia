#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void oprintf(const char *fmt, ...)
{
    char buf[512];
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
    (void)write(STDOUT_FILENO, buf, len);
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
    (void)write(STDERR_FILENO, buf, len);
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
    off_t offset = (off_t)(*skip_left);
    if (lseek(fd, offset, SEEK_CUR) >= 0) {
        *skip_left = 0;
        return 0;
    }
    unsigned char buf[256];
    while (*skip_left > 0) {
        size_t chunk = sizeof(buf);
        if (*skip_left < chunk) {
            chunk = (size_t)*skip_left;
        }
        ssize_t r = read(fd, buf, chunk);
        if (r <= 0) {
            return -1;
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
        ssize_t r = read(fd, buf, sizeof(buf));
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
                            if (write(STDOUT_FILENO, outbuf, out_len) != (ssize_t)out_len) {
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
                    if (write(STDOUT_FILENO, outbuf, out_len) != (ssize_t)out_len) {
                        return -1;
                    }
                    out_len = 0;
                }
                half = -1;
            }
        }
    }

    if (out_len > 0) {
        if (write(STDOUT_FILENO, outbuf, out_len) != (ssize_t)out_len) {
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
    uint64_t offset = 0;
    unsigned char buf[256];
    while (1) {
        size_t want = (size_t)columns;
        if (use_length && length < want) {
            want = (size_t)length;
        }
        ssize_t r = read(fd, buf, want);
        if (r < 0) {
            return -1;
        }
        if (r == 0) {
            break;
        }
        size_t len = (size_t)r;
        if (plain) {
            for (size_t i = 0; i < len; ++i) {
                oprintf(upper ? "%02X" : "%02x", buf[i]);
                if (i + 1 == len || ((i + 1) % (size_t)columns) == 0) {
                    oprintf("\n");
                }
            }
        } else {
            char line[512];
            size_t pos = 0;
            pos += (size_t)snprintf(line + pos, sizeof(line) - pos, "%08llx: ", (unsigned long long)offset);
            for (size_t i = 0; i < (size_t)columns; ++i) {
                if (i < len) {
                    pos += (size_t)snprintf(line + pos, sizeof(line) - pos, upper ? "%02X" : "%02x", buf[i]);
                } else {
                    pos += (size_t)snprintf(line + pos, sizeof(line) - pos, "  ");
                }
                if (group > 0 && ((i + 1) % (size_t)group) == 0) {
                    line[pos++] = ' ';
                }
            }
            line[pos++] = ' ';
            for (size_t i = 0; i < len; ++i) {
                unsigned char b = buf[i];
                line[pos++] = (b >= 0x20 && b <= 0x7e) ? (char)b : '.';
            }
            line[pos++] = '\n';
            (void)write(STDOUT_FILENO, line, pos);
        }
        offset += len;
        if (use_length) {
            length -= len;
            if (length == 0) {
                break;
            }
        }
    }
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
            group = atoi(optarg);
            if (group < 0) {
                group = 0;
            }
            break;
        case 'c':
            columns = atoi(optarg);
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
