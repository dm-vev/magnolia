#include <ctype.h>
#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <stdbool.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

struct arg_state {
    int argc;
    char **argv;
    int index;
    int exit_status;
};

enum length_mod {
    LEN_NONE = 0,
    LEN_HH,
    LEN_H,
    LEN_L,
    LEN_LL,
    LEN_J,
    LEN_Z,
    LEN_T,
    LEN_LD,
};

struct format_spec {
    bool alt;
    bool zero;
    bool left;
    bool sign;
    bool space;
    bool width_specified;
    bool width_from_arg;
    int width;
    bool precision_specified;
    bool precision_from_arg;
    int precision;
    enum length_mod length;
    char spec;
};

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

static int write_pad(size_t count, char ch)
{
    char buf[64];
    memset(buf, ch, sizeof(buf));
    while (count > 0) {
        size_t chunk = count > sizeof(buf) ? sizeof(buf) : count;
        if (write_all(STDOUT_FILENO, buf, chunk) != 0) {
            return -1;
        }
        count -= chunk;
    }
    return 0;
}

static int write_formatted(const char *fmt, ...)
{
    char stack[256];
    va_list ap;
    va_list ap_copy;
    va_start(ap, fmt);
    va_copy(ap_copy, ap);
    int n = vsnprintf(stack, sizeof(stack), fmt, ap);
    va_end(ap);
    if (n < 0) {
        va_end(ap_copy);
        errno = EIO;
        return -1;
    }
    size_t len = (size_t)n;
    if (len < sizeof(stack)) {
        int rc = write_all(STDOUT_FILENO, stack, len);
        va_end(ap_copy);
        return rc;
    }

    char *heap = malloc(len + 1);
    if (!heap) {
        va_end(ap_copy);
        errno = ENOMEM;
        return -1;
    }
    int n2 = vsnprintf(heap, len + 1, fmt, ap_copy);
    va_end(ap_copy);
    if (n2 < 0) {
        free(heap);
        errno = EIO;
        return -1;
    }
    int rc = write_all(STDOUT_FILENO, heap, len);
    free(heap);
    return rc;
}

static const char *next_arg(struct arg_state *state, bool *missing)
{
    if (state->index < state->argc) {
        const char *arg = state->argv[state->index];
        state->index++;
        if (missing) {
            *missing = false;
        }
        return arg ? arg : "";
    }
    if (missing) {
        *missing = true;
    }
    return "";
}

static long long parse_signed(const char *arg, bool missing, bool *ok)
{
    if (missing) {
        if (ok) {
            *ok = true;
        }
        return 0;
    }
    if (!arg || arg[0] == '\0') {
        if (ok) {
            *ok = false;
        }
        return 0;
    }
    errno = 0;
    char *end = NULL;
    long long val = strtoll(arg, &end, 0);
    if (errno != 0 || end == arg || *end != '\0') {
        if (ok) {
            *ok = false;
        }
        return 0;
    }
    if (ok) {
        *ok = true;
    }
    return val;
}

static unsigned long long parse_unsigned(const char *arg, bool missing, bool *ok)
{
    if (missing) {
        if (ok) {
            *ok = true;
        }
        return 0;
    }
    if (!arg || arg[0] == '\0') {
        if (ok) {
            *ok = false;
        }
        return 0;
    }
    errno = 0;
    char *end = NULL;
    unsigned long long val = strtoull(arg, &end, 0);
    if (errno != 0 || end == arg || *end != '\0') {
        if (ok) {
            *ok = false;
        }
        return 0;
    }
    if (ok) {
        *ok = true;
    }
    return val;
}

static double parse_double(const char *arg, bool missing, bool *ok)
{
    if (missing) {
        if (ok) {
            *ok = true;
        }
        return 0.0;
    }
    if (!arg || arg[0] == '\0') {
        if (ok) {
            *ok = false;
        }
        return 0.0;
    }
    errno = 0;
    char *end = NULL;
    double val = strtod(arg, &end);
    if (errno != 0 || end == arg || *end != '\0') {
        if (ok) {
            *ok = false;
        }
        return 0.0;
    }
    if (ok) {
        *ok = true;
    }
    return val;
}

static int parse_escape(const char *s, size_t *idx, unsigned char *out, bool *stop, bool allow_stop)
{
    char c = s[*idx];
    switch (c) {
    case '\\':
        *out = '\\';
        return 1;
    case 'a':
        *out = '\a';
        return 1;
    case 'b':
        *out = '\b';
        return 1;
    case 'f':
        *out = '\f';
        return 1;
    case 'n':
        *out = '\n';
        return 1;
    case 'r':
        *out = '\r';
        return 1;
    case 't':
        *out = '\t';
        return 1;
    case 'v':
        *out = '\v';
        return 1;
    case 'c':
        if (allow_stop) {
            if (stop) {
                *stop = true;
            }
            return 0;
        }
        *out = 'c';
        return 1;
    case '0': {
        unsigned value = 0;
        unsigned digits = 0;
        while (digits < 3) {
            char d = s[*idx + 1];
            if (d < '0' || d > '7') {
                break;
            }
            (*idx)++;
            value = (value << 3) + (unsigned)(d - '0');
            digits++;
        }
        *out = (unsigned char)(value & 0xffu);
        return 1;
    }
    case 'x': {
        unsigned value = 0;
        unsigned digits = 0;
        while (1) {
            char d = s[*idx + 1];
            if (!isxdigit((unsigned char)d)) {
                break;
            }
            (*idx)++;
            digits++;
            unsigned nibble;
            if (d >= '0' && d <= '9') {
                nibble = (unsigned)(d - '0');
            } else if (d >= 'a' && d <= 'f') {
                nibble = 10u + (unsigned)(d - 'a');
            } else {
                nibble = 10u + (unsigned)(d - 'A');
            }
            value = ((value << 4) | nibble) & 0xffu;
        }
        if (digits == 0) {
            *out = 'x';
            return 1;
        }
        *out = (unsigned char)value;
        return 1;
    }
    default:
        *out = (unsigned char)c;
        return 1;
    }
}

static const char *length_str(enum length_mod length)
{
    switch (length) {
    case LEN_HH:
        return "hh";
    case LEN_H:
        return "h";
    case LEN_L:
        return "l";
    case LEN_LL:
        return "ll";
    case LEN_J:
        return "j";
    case LEN_Z:
        return "z";
    case LEN_T:
        return "t";
    case LEN_LD:
        return "L";
    case LEN_NONE:
    default:
        return "";
    }
}

static int build_format(const struct format_spec *spec, char *buf, size_t cap)
{
    size_t pos = 0;
    if (cap == 0) {
        return -1;
    }
    buf[pos++] = '%';
    if (spec->left) {
        buf[pos++] = '-';
    }
    if (spec->sign) {
        buf[pos++] = '+';
    }
    if (spec->space) {
        buf[pos++] = ' ';
    }
    if (spec->alt) {
        buf[pos++] = '#';
    }
    if (spec->zero) {
        buf[pos++] = '0';
    }
    if (spec->width_specified) {
        int n = snprintf(buf + pos, cap - pos, "%d", spec->width);
        if (n < 0 || (size_t)n >= cap - pos) {
            return -1;
        }
        pos += (size_t)n;
    }
    if (spec->precision_specified) {
        if (pos + 1 >= cap) {
            return -1;
        }
        buf[pos++] = '.';
        int n = snprintf(buf + pos, cap - pos, "%d", spec->precision);
        if (n < 0 || (size_t)n >= cap - pos) {
            return -1;
        }
        pos += (size_t)n;
    }
    const char *len = length_str(spec->length);
    size_t len_len = strlen(len);
    if (pos + len_len + 2 > cap) {
        return -1;
    }
    if (len_len > 0) {
        memcpy(buf + pos, len, len_len);
        pos += len_len;
    }
    buf[pos++] = spec->spec;
    buf[pos] = '\0';
    return 0;
}

static bool parse_format_spec(const char *p, struct format_spec *spec, size_t *consumed)
{
    const char *start = p;
    memset(spec, 0, sizeof(*spec));
    while (*p != '\0') {
        switch (*p) {
        case '#':
            spec->alt = true;
            p++;
            continue;
        case '0':
            spec->zero = true;
            p++;
            continue;
        case '-':
            spec->left = true;
            p++;
            continue;
        case '+':
            spec->sign = true;
            p++;
            continue;
        case ' ':
            spec->space = true;
            p++;
            continue;
        default:
            break;
        }
        break;
    }

    if (*p == '*') {
        spec->width_specified = true;
        spec->width_from_arg = true;
        p++;
    } else if (isdigit((unsigned char)*p)) {
        spec->width_specified = true;
        long long v = 0;
        while (isdigit((unsigned char)*p)) {
            v = (v * 10) + (*p - '0');
            if (v > INT_MAX) {
                v = INT_MAX;
            }
            p++;
        }
        spec->width = (int)v;
    }

    if (*p == '.') {
        spec->precision_specified = true;
        p++;
        if (*p == '*') {
            spec->precision_from_arg = true;
            p++;
        } else {
            long long v = 0;
            while (isdigit((unsigned char)*p)) {
                v = (v * 10) + (*p - '0');
                if (v > INT_MAX) {
                    v = INT_MAX;
                }
                p++;
            }
            spec->precision = (int)v;
        }
    }

    if (p[0] == 'h' && p[1] == 'h') {
        spec->length = LEN_HH;
        p += 2;
    } else if (p[0] == 'h') {
        spec->length = LEN_H;
        p++;
    } else if (p[0] == 'l' && p[1] == 'l') {
        spec->length = LEN_LL;
        p += 2;
    } else if (p[0] == 'l') {
        spec->length = LEN_L;
        p++;
    } else if (p[0] == 'j') {
        spec->length = LEN_J;
        p++;
    } else if (p[0] == 'z') {
        spec->length = LEN_Z;
        p++;
    } else if (p[0] == 't') {
        spec->length = LEN_T;
        p++;
    } else if (p[0] == 'L') {
        spec->length = LEN_LD;
        p++;
    }

    if (*p == '\0') {
        return false;
    }
    spec->spec = *p;
    if (consumed) {
        *consumed = (size_t)(p - start) + 1;
    }
    return true;
}

static void warn_invalid_number(struct arg_state *state, const char *arg, bool missing)
{
    if (missing) {
        return;
    }
    eprintf("printf: %s: invalid number\n", arg ? arg : "");
    state->exit_status = 1;
}

static void resolve_width_precision(struct format_spec *spec, struct arg_state *state, bool *used_arg)
{
    if (spec->width_from_arg) {
        bool missing = false;
        const char *arg = next_arg(state, &missing);
        if (used_arg) {
            *used_arg = true;
        }
        bool ok = false;
        long long v = parse_signed(arg, missing, &ok);
        if (!ok) {
            warn_invalid_number(state, arg, missing);
            v = 0;
        }
        if (v < 0) {
            spec->left = true;
            if (v == LLONG_MIN) {
                v = (long long)INT_MAX;
            } else {
                v = -v;
            }
        }
        if (v > INT_MAX) {
            v = INT_MAX;
        }
        spec->width_specified = true;
        spec->width = (int)v;
    }

    if (spec->precision_from_arg) {
        bool missing = false;
        const char *arg = next_arg(state, &missing);
        if (used_arg) {
            *used_arg = true;
        }
        bool ok = false;
        long long v = parse_signed(arg, missing, &ok);
        if (!ok) {
            warn_invalid_number(state, arg, missing);
            v = 0;
        }
        if (v < 0) {
            spec->precision_specified = false;
        } else {
            if (v > INT_MAX) {
                v = INT_MAX;
            }
            spec->precision_specified = true;
            spec->precision = (int)v;
        }
    }
}

static int format_b(const char *arg, const struct format_spec *spec, bool *stop)
{
    if (!arg) {
        arg = "";
    }

    /* Buffer %b output so width/precision can be applied before writing. */
    size_t cap = 64;
    unsigned char *buf = malloc(cap);
    if (!buf) {
        errno = ENOMEM;
        return -1;
    }
    size_t len = 0;
    bool local_stop = false;

    for (size_t i = 0; arg[i] != '\0'; ++i) {
        unsigned char out = (unsigned char)arg[i];
        if (out == '\\') {
            if (arg[i + 1] == '\0') {
                out = '\\';
            } else {
                i++;
                if (!parse_escape(arg, &i, &out, &local_stop, true)) {
                    local_stop = true;
                    break;
                }
                if (local_stop) {
                    break;
                }
            }
        }
        if (len == cap) {
            if (cap > SIZE_MAX / 2) {
                free(buf);
                errno = ENOMEM;
                return -1;
            }
            size_t new_cap = cap * 2;
            unsigned char *next = realloc(buf, new_cap);
            if (!next) {
                free(buf);
                errno = ENOMEM;
                return -1;
            }
            buf = next;
            cap = new_cap;
        }
        buf[len++] = out;
    }

    if (local_stop) {
        if (len > 0 && write_all(STDOUT_FILENO, buf, len) != 0) {
            free(buf);
            return -1;
        }
        free(buf);
        if (stop) {
            *stop = true;
        }
        return 0;
    }

    size_t out_len = len;
    if (spec->precision_specified && spec->precision >= 0) {
        if ((size_t)spec->precision < out_len) {
            out_len = (size_t)spec->precision;
        }
    }
    size_t pad = 0;
    if (spec->width_specified && spec->width > 0) {
        size_t width = (size_t)spec->width;
        if (width > out_len) {
            pad = width - out_len;
        }
    }

    if (!spec->left) {
        if (write_pad(pad, ' ') != 0) {
            free(buf);
            return -1;
        }
    }
    if (out_len > 0 && write_all(STDOUT_FILENO, buf, out_len) != 0) {
        free(buf);
        return -1;
    }
    if (spec->left) {
        if (write_pad(pad, ' ') != 0) {
            free(buf);
            return -1;
        }
    }

    free(buf);
    return 0;
}

static int handle_conversion(struct format_spec *spec, struct arg_state *state, bool *used_arg, bool *stop)
{
    resolve_width_precision(spec, state, used_arg);

    char fmtbuf[64];
    if (spec->spec == 'n') {
        eprintf("printf: %%n is not supported\n");
        return 1;
    }

    switch (spec->spec) {
    case '%':
        if (build_format(spec, fmtbuf, sizeof(fmtbuf)) != 0) {
            eprintf("printf: invalid format\n");
            return 1;
        }
        if (write_formatted(fmtbuf) != 0) {
            eprintf("printf: stdout: %s\n", strerror(errno));
            return 1;
        }
        return 0;
    case 'b': {
        bool missing = false;
        const char *arg = next_arg(state, &missing);
        if (used_arg) {
            *used_arg = true;
        }
        if (format_b(arg, spec, stop) != 0) {
            eprintf("printf: stdout: %s\n", strerror(errno));
            return 1;
        }
        return 0;
    }
    case 's': {
        if (spec->length != LEN_NONE) {
            /* TODO: Support wide-character %ls once Magnolia exposes wchar/locale helpers. */
            eprintf("printf: %%s with length modifier is not supported\n");
            return 1;
        }
        bool missing = false;
        const char *arg = next_arg(state, &missing);
        if (used_arg) {
            *used_arg = true;
        }
        if (build_format(spec, fmtbuf, sizeof(fmtbuf)) != 0) {
            eprintf("printf: invalid format\n");
            return 1;
        }
        if (write_formatted(fmtbuf, arg ? arg : "") != 0) {
            eprintf("printf: stdout: %s\n", strerror(errno));
            return 1;
        }
        return 0;
    }
    case 'c': {
        if (spec->length != LEN_NONE) {
            /* TODO: Support wide-character %lc once Magnolia exposes wchar/locale helpers. */
            eprintf("printf: %%c with length modifier is not supported\n");
            return 1;
        }
        bool missing = false;
        const char *arg = next_arg(state, &missing);
        if (used_arg) {
            *used_arg = true;
        }
        unsigned char ch = 0;
        if (missing || !arg || arg[0] == '\0') {
            ch = 0;
        } else if (arg[0] == '\\') {
            size_t idx = 1;
            bool dummy_stop = false;
            unsigned char out = 0;
            if (parse_escape(arg, &idx, &out, &dummy_stop, false)) {
                ch = out;
            } else {
                ch = 0;
            }
        } else {
            bool ok = false;
            long long v = parse_signed(arg, false, &ok);
            if (ok) {
                ch = (unsigned char)v;
            } else {
                ch = (unsigned char)arg[0];
            }
        }
        if (build_format(spec, fmtbuf, sizeof(fmtbuf)) != 0) {
            eprintf("printf: invalid format\n");
            return 1;
        }
        if (write_formatted(fmtbuf, (int)ch) != 0) {
            eprintf("printf: stdout: %s\n", strerror(errno));
            return 1;
        }
        return 0;
    }
    case 'p': {
        if (spec->length != LEN_NONE) {
            eprintf("printf: %%p with length modifier is not supported\n");
            return 1;
        }
        bool missing = false;
        const char *arg = next_arg(state, &missing);
        if (used_arg) {
            *used_arg = true;
        }
        bool ok = false;
        unsigned long long v = parse_unsigned(arg, missing, &ok);
        if (!ok) {
            warn_invalid_number(state, arg, missing);
            v = 0;
        }
        if (build_format(spec, fmtbuf, sizeof(fmtbuf)) != 0) {
            eprintf("printf: invalid format\n");
            return 1;
        }
        void *ptr = (void *)(uintptr_t)v;
        if (write_formatted(fmtbuf, ptr) != 0) {
            eprintf("printf: stdout: %s\n", strerror(errno));
            return 1;
        }
        return 0;
    }
    case 'd':
    case 'i':
    case 'o':
    case 'u':
    case 'x':
    case 'X': {
        bool missing = false;
        const char *arg = next_arg(state, &missing);
        if (used_arg) {
            *used_arg = true;
        }
        bool ok = false;
        bool is_signed = (spec->spec == 'd' || spec->spec == 'i');
        if (build_format(spec, fmtbuf, sizeof(fmtbuf)) != 0) {
            eprintf("printf: invalid format\n");
            return 1;
        }
        if (is_signed) {
            long long v = parse_signed(arg, missing, &ok);
            if (!ok) {
                warn_invalid_number(state, arg, missing);
                v = 0;
            }
            switch (spec->length) {
            case LEN_NONE:
            case LEN_HH:
            case LEN_H: {
                int val = (int)v;
                if (write_formatted(fmtbuf, val) != 0) {
                    eprintf("printf: stdout: %s\n", strerror(errno));
                    return 1;
                }
                break;
            }
            case LEN_L: {
                long val = (long)v;
                if (write_formatted(fmtbuf, val) != 0) {
                    eprintf("printf: stdout: %s\n", strerror(errno));
                    return 1;
                }
                break;
            }
            case LEN_LL: {
                long long val = v;
                if (write_formatted(fmtbuf, val) != 0) {
                    eprintf("printf: stdout: %s\n", strerror(errno));
                    return 1;
                }
                break;
            }
            case LEN_J: {
                intmax_t val = (intmax_t)v;
                if (write_formatted(fmtbuf, val) != 0) {
                    eprintf("printf: stdout: %s\n", strerror(errno));
                    return 1;
                }
                break;
            }
            case LEN_Z: {
                ssize_t val = (ssize_t)v;
                if (write_formatted(fmtbuf, val) != 0) {
                    eprintf("printf: stdout: %s\n", strerror(errno));
                    return 1;
                }
                break;
            }
            case LEN_T: {
                ptrdiff_t val = (ptrdiff_t)v;
                if (write_formatted(fmtbuf, val) != 0) {
                    eprintf("printf: stdout: %s\n", strerror(errno));
                    return 1;
                }
                break;
            }
            case LEN_LD:
                eprintf("printf: invalid length modifier for integer conversion\n");
                return 1;
            }
        } else {
            unsigned long long v = parse_unsigned(arg, missing, &ok);
            if (!ok) {
                warn_invalid_number(state, arg, missing);
                v = 0;
            }
            switch (spec->length) {
            case LEN_NONE:
            case LEN_HH:
            case LEN_H: {
                unsigned int val = (unsigned int)v;
                if (write_formatted(fmtbuf, val) != 0) {
                    eprintf("printf: stdout: %s\n", strerror(errno));
                    return 1;
                }
                break;
            }
            case LEN_L: {
                unsigned long val = (unsigned long)v;
                if (write_formatted(fmtbuf, val) != 0) {
                    eprintf("printf: stdout: %s\n", strerror(errno));
                    return 1;
                }
                break;
            }
            case LEN_LL: {
                unsigned long long val = v;
                if (write_formatted(fmtbuf, val) != 0) {
                    eprintf("printf: stdout: %s\n", strerror(errno));
                    return 1;
                }
                break;
            }
            case LEN_J: {
                uintmax_t val = (uintmax_t)v;
                if (write_formatted(fmtbuf, val) != 0) {
                    eprintf("printf: stdout: %s\n", strerror(errno));
                    return 1;
                }
                break;
            }
            case LEN_Z: {
                size_t val = (size_t)v;
                if (write_formatted(fmtbuf, val) != 0) {
                    eprintf("printf: stdout: %s\n", strerror(errno));
                    return 1;
                }
                break;
            }
            case LEN_T: {
                size_t val = (size_t)v;
                if (write_formatted(fmtbuf, val) != 0) {
                    eprintf("printf: stdout: %s\n", strerror(errno));
                    return 1;
                }
                break;
            }
            case LEN_LD:
                eprintf("printf: invalid length modifier for integer conversion\n");
                return 1;
            }
        }
        return 0;
    }
    case 'f':
    case 'F':
    case 'e':
    case 'E':
    case 'g':
    case 'G':
    case 'a':
    case 'A': {
        bool missing = false;
        const char *arg = next_arg(state, &missing);
        if (used_arg) {
            *used_arg = true;
        }
        bool ok = false;
        double v = parse_double(arg, missing, &ok);
        if (!ok) {
            warn_invalid_number(state, arg, missing);
            v = 0.0;
        }
        if (spec->length != LEN_NONE && spec->length != LEN_L && spec->length != LEN_LD) {
            eprintf("printf: invalid length modifier for floating conversion\n");
            return 1;
        }
        if (build_format(spec, fmtbuf, sizeof(fmtbuf)) != 0) {
            eprintf("printf: invalid format\n");
            return 1;
        }
        if (spec->length == LEN_LD) {
            long double ld = (long double)v;
            if (write_formatted(fmtbuf, ld) != 0) {
                eprintf("printf: stdout: %s\n", strerror(errno));
                return 1;
            }
        } else {
            if (write_formatted(fmtbuf, v) != 0) {
                eprintf("printf: stdout: %s\n", strerror(errno));
                return 1;
            }
        }
        return 0;
    }
    default:
        eprintf("printf: %c: invalid format character\n", spec->spec);
        return 1;
    }
}

static void usage(void)
{
    eprintf("usage: printf format [arguments ...]\n");
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        usage();
        return 1;
    }

    const char *fmt = argv[1];
    struct arg_state state = {
        .argc = argc,
        .argv = argv,
        .index = 2,
        .exit_status = 0,
    };

    bool stop = false;
    bool first = true;
    /* Reuse the format string until arguments are exhausted. */
    while (!stop) {
        bool used_arg = false;
        if (!first && state.index >= state.argc) {
            break;
        }
        first = false;
        for (size_t i = 0; fmt[i] != '\0'; ++i) {
            char c = fmt[i];
            if (c == '\\') {
                if (fmt[i + 1] == '\0') {
                    unsigned char out = '\\';
                    if (write_all(STDOUT_FILENO, &out, 1) != 0) {
                        eprintf("printf: stdout: %s\n", strerror(errno));
                        return 1;
                    }
                    break;
                }
                i++;
                unsigned char out = 0;
                if (!parse_escape(fmt, &i, &out, &stop, true)) {
                    stop = true;
                    break;
                }
                if (write_all(STDOUT_FILENO, &out, 1) != 0) {
                    eprintf("printf: stdout: %s\n", strerror(errno));
                    return 1;
                }
                if (stop) {
                    break;
                }
                continue;
            }
            if (c != '%') {
                if (write_all(STDOUT_FILENO, &c, 1) != 0) {
                    eprintf("printf: stdout: %s\n", strerror(errno));
                    return 1;
                }
                continue;
            }

            struct format_spec spec;
            size_t consumed = 0;
            if (!parse_format_spec(fmt + i + 1, &spec, &consumed)) {
                eprintf("printf: missing format character\n");
                return 1;
            }
            i += consumed;

            if (handle_conversion(&spec, &state, &used_arg, &stop) != 0) {
                return 1;
            }
            if (stop) {
                break;
            }
        }

        if (stop || !used_arg) {
            break;
        }
    }

    return state.exit_status;
}
