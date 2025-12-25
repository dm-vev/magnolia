#include <errno.h>
#include <getopt.h>
#include <stdbool.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
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

static void print_help(void)
{
    printf("usage: tr [OPTION]... SET1 [SET2]\n");
    printf("  -d           delete characters in SET1\n");
    printf("  -s           squeeze repeated characters in SET1\n");
    printf("      --help   display this help and exit\n");
    printf("      --version output version information and exit\n");
    printf("note: only basic ranges like a-z and simple escapes are supported.\n");
}

static void print_version(void)
{
    printf("tr (%s)\n", g_version);
}

static int parse_escape(const char *s, size_t *consumed, unsigned char *out)
{
    if (s[0] != '\\') {
        *consumed = 1;
        *out = (unsigned char)s[0];
        return 0;
    }
    char c = s[1];
    if (c == '\0') {
        return -1;
    }
    if (c == 'n') {
        *consumed = 2;
        *out = '\n';
        return 0;
    }
    if (c == 't') {
        *consumed = 2;
        *out = '\t';
        return 0;
    }
    if (c == 'r') {
        *consumed = 2;
        *out = '\r';
        return 0;
    }
    if (c == '\\') {
        *consumed = 2;
        *out = '\\';
        return 0;
    }
    if (c == '0') {
        unsigned v = 0;
        size_t i = 2;
        size_t digits = 0;
        while (digits < 3 && s[i] >= '0' && s[i] <= '7') {
            v = (v * 8u) + (unsigned)(s[i] - '0');
            i++;
            digits++;
        }
        if (digits == 0) {
            return -1;
        }
        *consumed = i;
        *out = (unsigned char)(v & 0xff);
        return 0;
    }
    *consumed = 2;
    *out = (unsigned char)c;
    return 0;
}

static size_t expand_set(const char *spec, unsigned char *out, size_t cap)
{
    size_t n = 0;
    for (size_t i = 0; spec && spec[i] != '\0';) {
        unsigned char first;
        size_t c1 = 0;
        if (parse_escape(spec + i, &c1, &first) != 0) {
            return 0;
        }
        i += c1;
        if (spec[i] == '-' && spec[i + 1] != '\0') {
            unsigned char last;
            size_t c2 = 0;
            if (parse_escape(spec + i + 1, &c2, &last) != 0) {
                return 0;
            }
            i += 1 + c2;
            if (first <= last) {
                for (unsigned v = first; v <= last; ++v) {
                    if (n < cap) {
                        out[n] = (unsigned char)v;
                    }
                    n++;
                }
            } else {
                for (unsigned v = first;; --v) {
                    if (n < cap) {
                        out[n] = (unsigned char)v;
                    }
                    n++;
                    if (v == last) {
                        break;
                    }
                }
            }
            continue;
        }
        if (n < cap) {
            out[n] = first;
        }
        n++;
    }
    return n;
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

    bool del = false;
    bool squeeze = false;
    int opt;
    while ((opt = getopt(argc, argv, "ds")) != -1) {
        switch (opt) {
        case 'd':
            del = true;
            break;
        case 's':
            squeeze = true;
            break;
        default:
            eprintf("usage: tr [-d] [-s] SET1 [SET2]\n");
            return 1;
        }
    }

    if (optind >= argc) {
        eprintf("tr: missing operand\n");
        return 1;
    }
    const char *set1 = argv[optind++];
    const char *set2 = (optind < argc) ? argv[optind++] : NULL;
    if (!del && !set2) {
        eprintf("tr: missing operand\n");
        return 1;
    }

    unsigned char a[256];
    unsigned char b[256];
    size_t na = expand_set(set1, a, sizeof(a));
    size_t nb = set2 ? expand_set(set2, b, sizeof(b)) : 0;
    if (na == 0 || (!del && nb == 0)) {
        eprintf("tr: invalid set\n");
        return 1;
    }

    bool in_a[256] = {0};
    for (size_t i = 0; i < na && i < 256; ++i) {
        in_a[a[i]] = true;
    }
    unsigned char map[256];
    for (int i = 0; i < 256; ++i) {
        map[i] = (unsigned char)i;
    }
    if (!del) {
        unsigned char last = b[(nb ? nb : 1) - 1];
        for (size_t i = 0; i < na && i < 256; ++i) {
            unsigned char from = a[i];
            unsigned char to = (i < nb && i < 256) ? b[i] : last;
            map[from] = to;
        }
    }

    int prev_out = -1;
    char inbuf[512];
    unsigned char outbuf[512];
    while (1) {
        ssize_t r = read(STDIN_FILENO, inbuf, sizeof(inbuf));
        if (r < 0) {
            eprintf("tr: read: %s\n", strerror(errno));
            return 1;
        }
        if (r == 0) {
            break;
        }
        size_t out_len = 0;
        for (ssize_t i = 0; i < r; ++i) {
            unsigned char ch = (unsigned char)inbuf[i];
            if (del && in_a[ch]) {
                continue;
            }
            unsigned char out = del ? ch : map[ch];
            if (squeeze && in_a[out] && prev_out == (int)out) {
                continue;
            }
            prev_out = (int)out;
            outbuf[out_len++] = out;
        }
        size_t off = 0;
        while (off < out_len) {
            ssize_t w = write(STDOUT_FILENO, outbuf + off, out_len - off);
            if (w < 0) {
                eprintf("tr: write: %s\n", strerror(errno));
                return 1;
            }
            off += (size_t)w;
        }
    }
    return 0;
}

