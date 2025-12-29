#include <ctype.h>
#include <errno.h>
#include <getopt.h>
#include <locale.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

typedef struct {
    unsigned char *data;
    size_t len;
    size_t cap;
} byte_list_t;

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
    eprintf("usage: tr [-Ccs] [-d] [-s] [-t] string1 string2\n");
    eprintf("       tr [-Ccs] -d string1\n");
    eprintf("       tr [-Ccs] -s string1\n");
    eprintf("       tr [-Ccs] -d -s string1 string2\n");
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

static int write_all(int fd, const unsigned char *buf, size_t len)
{
    size_t off = 0;
    while (off < len) {
        ssize_t w = write(fd, buf + off, len - off);
        if (w < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }
        off += (size_t)w;
    }
    return 0;
}

static int list_reserve(byte_list_t *list, size_t extra)
{
    if (extra == 0) {
        return 0;
    }
    if (list->len > SIZE_MAX - extra) {
        errno = ENOMEM;
        return -1;
    }
    size_t needed = list->len + extra;
    if (needed <= list->cap) {
        return 0;
    }
    size_t next = list->cap ? list->cap : 64;
    while (next < needed) {
        if (next > SIZE_MAX / 2) {
            next = needed;
            break;
        }
        next *= 2;
    }
    unsigned char *tmp = (unsigned char *)realloc(list->data, next);
    if (!tmp) {
        errno = ENOMEM;
        return -1;
    }
    list->data = tmp;
    list->cap = next;
    return 0;
}

static int list_append(byte_list_t *list, unsigned char ch)
{
    if (list_reserve(list, 1) != 0) {
        return -1;
    }
    list->data[list->len++] = ch;
    return 0;
}

static int list_append_n(byte_list_t *list, unsigned char ch, size_t count)
{
    if (count == 0) {
        return 0;
    }
    if (list_reserve(list, count) != 0) {
        return -1;
    }
    memset(list->data + list->len, ch, count);
    list->len += count;
    return 0;
}

static bool class_match(const char *name, unsigned char ch)
{
    if (strcmp(name, "alnum") == 0) {
        return isalnum(ch) != 0;
    }
    if (strcmp(name, "alpha") == 0) {
        return isalpha(ch) != 0;
    }
    if (strcmp(name, "blank") == 0) {
        return isblank(ch) != 0;
    }
    if (strcmp(name, "cntrl") == 0) {
        return iscntrl(ch) != 0;
    }
    if (strcmp(name, "digit") == 0) {
        return isdigit(ch) != 0;
    }
    if (strcmp(name, "graph") == 0) {
        return isgraph(ch) != 0;
    }
    if (strcmp(name, "lower") == 0) {
        return islower(ch) != 0;
    }
    if (strcmp(name, "print") == 0) {
        return isprint(ch) != 0;
    }
    if (strcmp(name, "punct") == 0) {
        return ispunct(ch) != 0;
    }
    if (strcmp(name, "space") == 0) {
        return isspace(ch) != 0;
    }
    if (strcmp(name, "upper") == 0) {
        return isupper(ch) != 0;
    }
    if (strcmp(name, "xdigit") == 0) {
        return isxdigit(ch) != 0;
    }
    return false;
}

static bool class_name_valid(const char *name)
{
    static const char *kClasses[] = {
        "alnum",
        "alpha",
        "blank",
        "cntrl",
        "digit",
        "graph",
        "lower",
        "print",
        "punct",
        "space",
        "upper",
        "xdigit",
    };
    for (size_t i = 0; i < sizeof(kClasses) / sizeof(kClasses[0]); ++i) {
        if (strcmp(name, kClasses[i]) == 0) {
            return true;
        }
    }
    return false;
}

static int expand_class(const char *name, byte_list_t *out)
{
    if (!class_name_valid(name)) {
        return -1;
    }
    for (int i = 0; i < 256; ++i) {
        unsigned char ch = (unsigned char)i;
        if (class_match(name, ch)) {
            if (list_append(out, ch) != 0) {
                return -1;
            }
        }
    }
    return 0;
}

static int parse_escape(const char *s, size_t *consumed, unsigned char *out)
{
    if (s[0] != '\\') {
        *consumed = 1;
        *out = (unsigned char)s[0];
        return 0;
    }
    if (s[1] == '\0') {
        return -1;
    }
    if (s[1] >= '0' && s[1] <= '7') {
        unsigned value = 0;
        size_t i = 1;
        size_t digits = 0;
        while (digits < 3 && s[i] >= '0' && s[i] <= '7') {
            value = (value * 8u) + (unsigned)(s[i] - '0');
            i++;
            digits++;
        }
        *consumed = i;
        *out = (unsigned char)(value & 0xffu);
        return 0;
    }
    switch (s[1]) {
    case 'a':
        *out = '\a';
        break;
    case 'b':
        *out = '\b';
        break;
    case 'f':
        *out = '\f';
        break;
    case 'n':
        *out = '\n';
        break;
    case 'r':
        *out = '\r';
        break;
    case 't':
        *out = '\t';
        break;
    case 'v':
        *out = '\v';
        break;
    case '\\':
        *out = '\\';
        break;
    default:
        *out = (unsigned char)s[1];
        break;
    }
    *consumed = 2;
    return 0;
}

static int parse_single_char(const char *s, size_t *consumed, unsigned char *out)
{
    if (parse_escape(s, consumed, out) != 0) {
        return -1;
    }
    return 0;
}

static int parse_equiv_or_collate(const char *s,
                                  size_t start,
                                  size_t end,
                                  unsigned char *out)
{
    if (start >= end) {
        return -1;
    }
    size_t consumed = 0;
    if (parse_escape(s + start, &consumed, out) != 0) {
        return -1;
    }
    if (start + consumed != end) {
        return -1;
    }
    return 0;
}

static int parse_repeat_count(const char *s, size_t len, size_t *out)
{
    if (len == 0) {
        *out = 0;
        return 0;
    }
    size_t base = (s[0] == '0') ? 8u : 10u;
    size_t value = 0;
    for (size_t i = 0; i < len; ++i) {
        unsigned digit = (unsigned)(s[i] - '0');
        if (digit >= base) {
            return -1;
        }
        if (value > (SIZE_MAX - digit) / base) {
            return -1;
        }
        value = value * base + digit;
    }
    *out = value;
    return 0;
}

static int expand_set(const char *spec,
                      byte_list_t *out,
                      bool allow_repeat_to_len,
                      size_t target_len)
{
    size_t i = 0;
    while (spec && spec[i] != '\0') {
        if (spec[i] == '[') {
            if (spec[i + 1] == ':' && spec[i + 2] != '\0') {
                size_t j = i + 2;
                while (spec[j] != '\0') {
                    if (spec[j] == ':' && spec[j + 1] == ']') {
                        size_t name_len = j - (i + 2);
                        if (name_len == 0 || name_len >= 32) {
                            return -1;
                        }
                        char name[32];
                        memcpy(name, spec + i + 2, name_len);
                        name[name_len] = '\0';
                        if (expand_class(name, out) != 0) {
                            return -1;
                        }
                        i = j + 2;
                        goto next_char;
                    }
                    j++;
                }
            } else if (spec[i + 1] == '=' && spec[i + 2] != '\0') {
                size_t j = i + 2;
                while (spec[j] != '\0') {
                    if (spec[j] == '=' && spec[j + 1] == ']') {
                        unsigned char ch = 0;
                        if (parse_equiv_or_collate(spec, i + 2, j, &ch) != 0) {
                            return -1;
                        }
                        if (list_append(out, ch) != 0) {
                            return -1;
                        }
                        i = j + 2;
                        goto next_char;
                    }
                    j++;
                }
            } else if (spec[i + 1] == '.' && spec[i + 2] != '\0') {
                size_t j = i + 2;
                while (spec[j] != '\0') {
                    if (spec[j] == '.' && spec[j + 1] == ']') {
                        unsigned char ch = 0;
                        if (parse_equiv_or_collate(spec, i + 2, j, &ch) != 0) {
                            return -1;
                        }
                        if (list_append(out, ch) != 0) {
                            return -1;
                        }
                        i = j + 2;
                        goto next_char;
                    }
                    j++;
                }
            } else {
                unsigned char ch = 0;
                size_t consumed = 0;
                if (parse_single_char(spec + i + 1, &consumed, &ch) == 0) {
                    size_t star_pos = i + 1 + consumed;
                    if (spec[star_pos] == '*') {
                        size_t j = star_pos + 1;
                        while (spec[j] != '\0' && spec[j] != ']') {
                            if (spec[j] < '0' || spec[j] > '9') {
                                return -1;
                            }
                            j++;
                        }
                        if (spec[j] != ']') {
                            return -1;
                        }
                        size_t count = 0;
                        size_t digits_len = j - (star_pos + 1);
                        if (digits_len == 0) {
                            if (!allow_repeat_to_len) {
                                return -1;
                            }
                            if (target_len > out->len) {
                                size_t need = target_len - out->len;
                                if (list_append_n(out, ch, need) != 0) {
                                    return -1;
                                }
                            }
                        } else {
                            if (parse_repeat_count(spec + star_pos + 1, digits_len, &count) != 0) {
                                return -1;
                            }
                            if (list_append_n(out, ch, count) != 0) {
                                return -1;
                            }
                        }
                        i = j + 1;
                        goto next_char;
                    }
                }
            }
        }

        unsigned char first = 0;
        size_t consumed = 0;
        if (parse_single_char(spec + i, &consumed, &first) != 0) {
            return -1;
        }
        size_t next = i + consumed;
        if (spec[next] == '-' && spec[next + 1] != '\0') {
            unsigned char last = 0;
            size_t consumed2 = 0;
            if (parse_single_char(spec + next + 1, &consumed2, &last) == 0) {
                if (first > last) {
                    return -1;
                }
                for (unsigned v = first; v <= last; ++v) {
                    if (list_append(out, (unsigned char)v) != 0) {
                        return -1;
                    }
                }
                i = next + 1 + consumed2;
                goto next_char;
            }
        }
        if (list_append(out, first) != 0) {
            return -1;
        }
        i += consumed;

    next_char:
        ;
    }
    return 0;
}

static void list_free(byte_list_t *list)
{
    free(list->data);
    list->data = NULL;
    list->len = 0;
    list->cap = 0;
}

static void build_membership(const byte_list_t *list, bool *mask)
{
    for (int i = 0; i < 256; ++i) {
        mask[i] = false;
    }
    for (size_t i = 0; i < list->len; ++i) {
        mask[list->data[i]] = true;
    }
}

int main(int argc, char **argv)
{
    (void)setlocale(LC_CTYPE, "");

    bool complement = false;
    bool del = false;
    bool squeeze = false;
    bool truncate_set1 = false;
    int opt;
    while ((opt = getopt(argc, argv, "Ccdst")) != -1) {
        switch (opt) {
        case 'C':
        case 'c':
            complement = true;
            break;
        case 'd':
            del = true;
            break;
        case 's':
            squeeze = true;
            break;
        case 't':
            truncate_set1 = true;
            break;
        default:
            usage();
            return 1;
        }
    }

    int remaining = argc - optind;
    if (del) {
        if (squeeze) {
            if (remaining != 1 && remaining != 2) {
                usage();
                return 1;
            }
        } else if (remaining != 1) {
            usage();
            return 1;
        }
    } else {
        if (remaining != 2) {
            usage();
            return 1;
        }
    }

    const char *set1_spec = (remaining >= 1) ? argv[optind] : NULL;
    const char *set2_spec = (remaining >= 2) ? argv[optind + 1] : NULL;

    if (!set1_spec || set1_spec[0] == '\0') {
        eprintf("tr: empty string1\n");
        return 1;
    }

    byte_list_t set1_raw = {0};
    if (expand_set(set1_spec, &set1_raw, false, 0) != 0) {
        eprintf("tr: invalid string1\n");
        list_free(&set1_raw);
        return 1;
    }
    if (set1_raw.len == 0) {
        eprintf("tr: empty string1\n");
        list_free(&set1_raw);
        return 1;
    }

    bool set1_mask[256];
    build_membership(&set1_raw, set1_mask);

    byte_list_t set1 = {0};
    if (complement) {
        for (int i = 0; i < 256; ++i) {
            if (!set1_mask[i]) {
                if (list_append(&set1, (unsigned char)i) != 0) {
                    eprintf("tr: out of memory\n");
                    list_free(&set1_raw);
                    list_free(&set1);
                    return 1;
                }
            }
        }
    } else {
        set1 = set1_raw;
    }

    byte_list_t set2 = {0};
    if (!del) {
        if (!set2_spec || set2_spec[0] == '\0') {
            eprintf("tr: empty string2\n");
            if (complement) {
                list_free(&set1);
            }
            list_free(&set1_raw);
            return 1;
        }
        if (expand_set(set2_spec, &set2, true, set1.len) != 0) {
            eprintf("tr: invalid string2\n");
            if (complement) {
                list_free(&set1);
            }
            list_free(&set1_raw);
            list_free(&set2);
            return 1;
        }
        if (set2.len == 0) {
            eprintf("tr: empty string2\n");
            if (complement) {
                list_free(&set1);
            }
            list_free(&set1_raw);
            list_free(&set2);
            return 1;
        }
    } else if (squeeze && set2_spec) {
        if (expand_set(set2_spec, &set2, false, 0) != 0) {
            eprintf("tr: invalid string2\n");
            if (complement) {
                list_free(&set1);
            }
            list_free(&set1_raw);
            list_free(&set2);
            return 1;
        }
    }

    if (truncate_set1 && !del) {
        if (set2.len < set1.len) {
            set1.len = set2.len;
        }
    }

    if (!del && set1.len == 0) {
        eprintf("tr: empty string1\n");
        if (complement) {
            list_free(&set1);
        }
        list_free(&set1_raw);
        list_free(&set2);
        return 1;
    }

    bool delete_mask[256];
    if (del) {
        if (complement) {
            for (int i = 0; i < 256; ++i) {
                delete_mask[i] = !set1_mask[i];
            }
        } else {
            build_membership(&set1, delete_mask);
        }
    }

    bool squeeze_mask[256];
    bool do_squeeze = squeeze;
    if (do_squeeze) {
        const byte_list_t *squeeze_set = NULL;
        if (!del) {
            squeeze_set = &set2;
        } else if (set2_spec) {
            squeeze_set = &set2;
        } else {
            squeeze_set = &set1;
        }
        build_membership(squeeze_set, squeeze_mask);
        if (squeeze_set == &set1 && complement) {
            for (int i = 0; i < 256; ++i) {
                squeeze_mask[i] = !set1_mask[i];
            }
        }
    }

    unsigned char map[256];
    if (!del) {
        for (int i = 0; i < 256; ++i) {
            map[i] = (unsigned char)i;
        }
        unsigned char last = set2.data[set2.len - 1];
        for (size_t i = 0; i < set1.len; ++i) {
            unsigned char from = set1.data[i];
            unsigned char to = (i < set2.len) ? set2.data[i] : last;
            map[from] = to;
        }
    }

    int prev_out = -1;
    unsigned char inbuf[4096];
    unsigned char outbuf[4096];
    while (1) {
        ssize_t r = read_retry(STDIN_FILENO, inbuf, sizeof(inbuf));
        if (r < 0) {
            eprintf("tr: read: %s\n", strerror(errno));
            break;
        }
        if (r == 0) {
            break;
        }
        size_t out_len = 0;
        for (ssize_t i = 0; i < r; ++i) {
            unsigned char ch = inbuf[i];
            if (del && delete_mask[ch]) {
                continue;
            }
            unsigned char out = del ? ch : map[ch];
            if (do_squeeze && squeeze_mask[out] && prev_out == (int)out) {
                continue;
            }
            prev_out = (int)out;
            outbuf[out_len++] = out;
        }
        if (write_all(STDOUT_FILENO, outbuf, out_len) != 0) {
            eprintf("tr: write: %s\n", strerror(errno));
            break;
        }
    }

    if (complement) {
        list_free(&set1);
    }
    list_free(&set1_raw);
    list_free(&set2);
    return 0;
}
