#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <stdbool.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

enum number_mode {
    NUMBER_NONE = 0,
    NUMBER_ALL,
    NUMBER_NONBLANK,
};

struct cat_options {
    enum number_mode number_mode;
    bool squeeze_blank;
    bool show_ends;
    bool show_tabs;
    bool show_nonprinting;
    bool unbuffered;
};

struct cat_state {
    unsigned long long line_no;
    bool at_line_start;
    bool blank_run;
};

struct outbuf {
    int fd;
    bool unbuffered;
    char buf[4096];
    size_t len;
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

static int out_flush(struct outbuf *out)
{
    if (out->len == 0) {
        return 0;
    }
    if (write_all(out->fd, out->buf, out->len) != 0) {
        return -1;
    }
    out->len = 0;
    return 0;
}

static int out_write(struct outbuf *out, const char *buf, size_t len)
{
    if (len == 0) {
        return 0;
    }
    if (out->unbuffered) {
        return write_all(out->fd, buf, len);
    }
    if (len >= sizeof(out->buf)) {
        if (out_flush(out) != 0) {
            return -1;
        }
        return write_all(out->fd, buf, len);
    }
    if (out->len + len > sizeof(out->buf)) {
        if (out_flush(out) != 0) {
            return -1;
        }
    }
    memcpy(out->buf + out->len, buf, len);
    out->len += len;
    return 0;
}

static int emit_line_number(struct outbuf *out, unsigned long long *line_no)
{
    char numbuf[32];
    int n = snprintf(numbuf, sizeof(numbuf), "%6llu\t", (unsigned long long)(*line_no));
    if (n < 0) {
        errno = EIO;
        return -1;
    }
    *line_no += 1;
    return out_write(out, numbuf, (size_t)n);
}

static int emit_visible_char(struct outbuf *out, unsigned char c, const struct cat_options *opt)
{
    if (c == '\t') {
        if (opt->show_tabs) {
            return out_write(out, "^I", 2);
        }
        char tab = '\t';
        return out_write(out, &tab, 1);
    }
    if (!opt->show_nonprinting) {
        char ch = (char)c;
        return out_write(out, &ch, 1);
    }

    char buf[4];
    size_t len = 0;
    if (c & 0x80) {
        buf[len++] = 'M';
        buf[len++] = '-';
        c &= 0x7f;
    }
    if (c < 0x20) {
        buf[len++] = '^';
        buf[len++] = (char)(c + 0x40);
        return out_write(out, buf, len);
    }
    if (c == 0x7f) {
        buf[len++] = '^';
        buf[len++] = '?';
        return out_write(out, buf, len);
    }
    buf[len++] = (char)c;
    return out_write(out, buf, len);
}

static int cat_plain(int fd, const char *name)
{
    unsigned char buf[512];
    while (1) {
        ssize_t r = read_retry(fd, buf, sizeof(buf));
        if (r < 0) {
            eprintf("cat: %s: %s\n", name, strerror(errno));
            return 1;
        }
        if (r == 0) {
            return 0;
        }
        if (write_all(STDOUT_FILENO, buf, (size_t)r) != 0) {
            eprintf("cat: stdout: %s\n", strerror(errno));
            return 1;
        }
    }
}

static int cat_stream(int fd, const char *name, const struct cat_options *opt, struct cat_state *state)
{
    unsigned char buf[512];
    struct outbuf out = {
        .fd = STDOUT_FILENO,
        .unbuffered = opt->unbuffered,
        .len = 0,
    };

    while (1) {
        ssize_t r = read_retry(fd, buf, sizeof(buf));
        if (r < 0) {
            int err = errno;
            if (out_flush(&out) != 0) {
                eprintf("cat: stdout: %s\n", strerror(errno));
                return 1;
            }
            eprintf("cat: %s: %s\n", name, strerror(err));
            return 1;
        }
        if (r == 0) {
            break;
        }
        for (ssize_t i = 0; i < r; ++i) {
            unsigned char c = buf[i];
            if (state->at_line_start) {
                if (c == '\n') {
                    if (opt->squeeze_blank && state->blank_run) {
                        continue;
                    }
                    if (opt->number_mode == NUMBER_ALL) {
                        if (emit_line_number(&out, &state->line_no) != 0) {
                            goto write_error;
                        }
                    }
                    if (opt->show_ends) {
                        if (out_write(&out, "$", 1) != 0) {
                            goto write_error;
                        }
                    }
                    if (out_write(&out, "\n", 1) != 0) {
                        goto write_error;
                    }
                    state->blank_run = true;
                    state->at_line_start = true;
                    continue;
                }
                if (opt->squeeze_blank) {
                    state->blank_run = false;
                }
                if (opt->number_mode != NUMBER_NONE) {
                    if (emit_line_number(&out, &state->line_no) != 0) {
                        goto write_error;
                    }
                }
                state->at_line_start = false;
            }

            if (c == '\n') {
                if (opt->show_ends) {
                    if (out_write(&out, "$", 1) != 0) {
                        goto write_error;
                    }
                }
                if (out_write(&out, "\n", 1) != 0) {
                    goto write_error;
                }
                state->at_line_start = true;
                continue;
            }
            if (emit_visible_char(&out, c, opt) != 0) {
                goto write_error;
            }
        }
    }

    if (out_flush(&out) != 0) {
        goto write_error;
    }
    return 0;

write_error:
    eprintf("cat: stdout: %s\n", strerror(errno));
    return 1;
}

static bool needs_processing(const struct cat_options *opt)
{
    return opt->number_mode != NUMBER_NONE || opt->squeeze_blank || opt->show_ends ||
           opt->show_tabs || opt->show_nonprinting;
}

static int cat_fd(int fd, const char *name, const struct cat_options *opt, struct cat_state *state)
{
    if (!needs_processing(opt)) {
        return cat_plain(fd, name);
    }
    return cat_stream(fd, name, opt, state);
}

static int cat_one(const char *path, const struct cat_options *opt, struct cat_state *state)
{
    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        eprintf("cat: %s: %s\n", path, strerror(errno));
        return 1;
    }
    int rc = cat_fd(fd, path, opt, state);
    (void)close(fd);
    return rc;
}

int main(int argc, char **argv)
{
    struct cat_options opt = {
        .number_mode = NUMBER_NONE,
        .squeeze_blank = false,
        .show_ends = false,
        .show_tabs = false,
        .show_nonprinting = false,
        .unbuffered = false,
    };
    struct cat_state state = {
        .line_no = 1,
        .at_line_start = true,
        .blank_run = false,
    };

    int ch;
    while ((ch = getopt(argc, argv, "benstuv")) != -1) {
        switch (ch) {
        case 'b':
            opt.number_mode = NUMBER_NONBLANK;
            break;
        case 'e':
            opt.show_ends = true;
            opt.show_nonprinting = true;
            break;
        case 'n':
            if (opt.number_mode != NUMBER_NONBLANK) {
                opt.number_mode = NUMBER_ALL;
            }
            break;
        case 's':
            opt.squeeze_blank = true;
            break;
        case 't':
            opt.show_tabs = true;
            opt.show_nonprinting = true;
            break;
        case 'u':
            /* BSD: unbuffered I/O; we only disable internal output buffering. */
            opt.unbuffered = true;
            break;
        case 'v':
            opt.show_nonprinting = true;
            break;
        default:
            eprintf("usage: cat [-benstuv] [file ...]\n");
            return 1;
        }
    }

    if (optind >= argc) {
        return cat_fd(STDIN_FILENO, "-", &opt, &state);
    }

    int failed = 0;
    for (int i = optind; i < argc; ++i) {
        const char *path = argv[i];
        if (path == NULL) {
            continue;
        }
        if (strcmp(path, "-") == 0) {
            failed |= cat_fd(STDIN_FILENO, "-", &opt, &state);
            continue;
        }
        failed |= cat_one(path, &opt, &state);
    }
    return failed ? 1 : 0;
}
