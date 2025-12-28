#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define LINE_BYTES 16

enum format_mode {
    F_CANONICAL,
    F_BYTE_OCTAL,
    F_CHAR,
    F_SHORT_DEC,
    F_SHORT_OCT,
    F_SHORT_HEX,
};

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

static void render_char(unsigned char b, char out[4])
{
    out[0] = ' ';
    out[1] = ' ';
    out[2] = ' ';
    out[3] = '\0';
    switch (b) {
    case '\0':
        out[1] = '\\';
        out[2] = '0';
        return;
    case '\n':
        out[1] = '\\';
        out[2] = 'n';
        return;
    case '\r':
        out[1] = '\\';
        out[2] = 'r';
        return;
    case '\t':
        out[1] = '\\';
        out[2] = 't';
        return;
    case '\b':
        out[1] = '\\';
        out[2] = 'b';
        return;
    case '\f':
        out[1] = '\\';
        out[2] = 'f';
        return;
    case '\v':
        out[1] = '\\';
        out[2] = 'v';
        return;
    case '\\':
        out[1] = '\\';
        out[2] = '\\';
        return;
    default:
        break;
    }
    if (b >= 0x20 && b <= 0x7e) {
        out[2] = (char)b;
        return;
    }
    out[1] = '.';
    out[2] = ' ';
}

static void print_canonical(uint64_t offset, const unsigned char *buf, size_t len)
{
    char line[256];
    size_t pos = 0;
    pos += (size_t)snprintf(line + pos, sizeof(line) - pos, "%08llx  ", (unsigned long long)offset);
    for (size_t i = 0; i < LINE_BYTES; ++i) {
        if (i < len) {
            pos += (size_t)snprintf(line + pos, sizeof(line) - pos, "%02x ", buf[i]);
        } else {
            pos += (size_t)snprintf(line + pos, sizeof(line) - pos, "   ");
        }
        if (i == 7) {
            pos += (size_t)snprintf(line + pos, sizeof(line) - pos, " ");
        }
    }
    pos += (size_t)snprintf(line + pos, sizeof(line) - pos, " |");
    for (size_t i = 0; i < LINE_BYTES; ++i) {
        if (i < len) {
            unsigned char b = buf[i];
            line[pos++] = (b >= 0x20 && b <= 0x7e) ? (char)b : '.';
        } else {
            line[pos++] = ' ';
        }
    }
    line[pos++] = '|';
    line[pos++] = '\n';
    (void)write(STDOUT_FILENO, line, pos);
}

static void print_byte_octal(uint64_t offset, const unsigned char *buf, size_t len)
{
    char line[256];
    size_t pos = 0;
    pos += (size_t)snprintf(line + pos, sizeof(line) - pos, "%08llx ", (unsigned long long)offset);
    for (size_t i = 0; i < LINE_BYTES; ++i) {
        if (i < len) {
            pos += (size_t)snprintf(line + pos, sizeof(line) - pos, " %03o", buf[i]);
        } else {
            pos += (size_t)snprintf(line + pos, sizeof(line) - pos, "    ");
        }
    }
    line[pos++] = '\n';
    (void)write(STDOUT_FILENO, line, pos);
}

static void print_char(uint64_t offset, const unsigned char *buf, size_t len)
{
    char line[256];
    size_t pos = 0;
    pos += (size_t)snprintf(line + pos, sizeof(line) - pos, "%08llx ", (unsigned long long)offset);
    for (size_t i = 0; i < LINE_BYTES; ++i) {
        char repr[4];
        if (i < len) {
            render_char(buf[i], repr);
            pos += (size_t)snprintf(line + pos, sizeof(line) - pos, " %s", repr + 1);
        } else {
            pos += (size_t)snprintf(line + pos, sizeof(line) - pos, "   ");
        }
    }
    line[pos++] = '\n';
    (void)write(STDOUT_FILENO, line, pos);
}

static void print_short(uint64_t offset, const unsigned char *buf, size_t len, enum format_mode mode)
{
    char line[256];
    size_t pos = 0;
    pos += (size_t)snprintf(line + pos, sizeof(line) - pos, "%08llx ", (unsigned long long)offset);
    for (size_t i = 0; i < LINE_BYTES; i += 2) {
        if (i + 1 < len) {
            uint16_t word = (uint16_t)buf[i] | ((uint16_t)buf[i + 1] << 8);
            if (mode == F_SHORT_DEC) {
                pos += (size_t)snprintf(line + pos, sizeof(line) - pos, " %05u", (unsigned)word);
            } else if (mode == F_SHORT_OCT) {
                pos += (size_t)snprintf(line + pos, sizeof(line) - pos, " %06o", (unsigned)word);
            } else {
                pos += (size_t)snprintf(line + pos, sizeof(line) - pos, " %04x", (unsigned)word);
            }
        } else {
            if (mode == F_SHORT_DEC) {
                pos += (size_t)snprintf(line + pos, sizeof(line) - pos, "      ");
            } else if (mode == F_SHORT_OCT) {
                pos += (size_t)snprintf(line + pos, sizeof(line) - pos, "       ");
            } else {
                pos += (size_t)snprintf(line + pos, sizeof(line) - pos, "     ");
            }
        }
    }
    line[pos++] = '\n';
    (void)write(STDOUT_FILENO, line, pos);
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

static int hexdump_fd(int fd, const char *name, enum format_mode mode, bool verbose,
                      uint64_t *offset, uint64_t *remaining, uint64_t *skip)
{
    unsigned char prev[LINE_BYTES];
    size_t prev_len = 0;
    bool suppressed = false;

    while (1) {
        if (*skip > 0) {
            if (skip_bytes(fd, skip) != 0) {
                return -1;
            }
        }
        size_t want = LINE_BYTES;
        if (remaining && *remaining < want) {
            want = (size_t)*remaining;
        }
        unsigned char buf[LINE_BYTES];
        ssize_t r = read(fd, buf, want);
        if (r < 0) {
            eprintf("hexdump: %s: %s\n", name, strerror(errno));
            return -1;
        }
        if (r == 0) {
            break;
        }
        size_t len = (size_t)r;
        if (remaining) {
            *remaining -= len;
        }
        bool same = !verbose && prev_len == len && memcmp(prev, buf, len) == 0;
        if (same) {
            if (!suppressed) {
                oprintf("*\n");
                suppressed = true;
            }
        } else {
            suppressed = false;
            if (mode == F_CANONICAL) {
                print_canonical(*offset, buf, len);
            } else if (mode == F_BYTE_OCTAL) {
                print_byte_octal(*offset, buf, len);
            } else if (mode == F_CHAR) {
                print_char(*offset, buf, len);
            } else {
                print_short(*offset, buf, len, mode);
            }
            memcpy(prev, buf, len);
            prev_len = len;
        }
        *offset += len;
        if (remaining && *remaining == 0) {
            break;
        }
    }
    return 0;
}

int main(int argc, char **argv)
{
    enum format_mode mode = F_CANONICAL;
    bool verbose = false;
    uint64_t length = 0;
    uint64_t skip = 0;
    bool use_length = false;

    int opt;
    while ((opt = getopt(argc, argv, "bcCdoxn:s:v")) != -1) {
        switch (opt) {
        case 'b':
            mode = F_BYTE_OCTAL;
            break;
        case 'c':
            mode = F_CHAR;
            break;
        case 'C':
            mode = F_CANONICAL;
            break;
        case 'd':
            mode = F_SHORT_DEC;
            break;
        case 'o':
            mode = F_SHORT_OCT;
            break;
        case 'x':
            mode = F_SHORT_HEX;
            break;
        case 'n':
            if (parse_size(optarg, &length) != 0) {
                eprintf("hexdump: invalid length '%s'\n", optarg);
                return 1;
            }
            use_length = true;
            break;
        case 's':
            if (parse_size(optarg, &skip) != 0) {
                eprintf("hexdump: invalid skip '%s'\n", optarg);
                return 1;
            }
            break;
        case 'v':
            verbose = true;
            break;
        default:
            eprintf("usage: hexdump [-bcdoxC] [-n length] [-s offset] [-v] [file ...]\n");
            return 1;
        }
    }

    uint64_t offset = 0;
    uint64_t remaining = length;
    uint64_t skip_left = skip;
    int rc = 0;

    if (optind >= argc) {
        if (hexdump_fd(STDIN_FILENO, "-", mode, verbose, &offset,
                       use_length ? &remaining : NULL, &skip_left) != 0) {
            rc = 1;
        }
    } else {
        for (int i = optind; i < argc; ++i) {
            const char *path = argv[i];
            int fd = open(path, O_RDONLY);
            if (fd < 0) {
                eprintf("hexdump: %s: %s\n", path, strerror(errno));
                rc = 1;
                continue;
            }
            if (hexdump_fd(fd, path, mode, verbose, &offset,
                           use_length ? &remaining : NULL, &skip_left) != 0) {
                rc = 1;
            }
            close(fd);
            if (use_length && remaining == 0) {
                break;
            }
        }
    }

    oprintf("%08llx\n", (unsigned long long)offset);
    return rc;
}
