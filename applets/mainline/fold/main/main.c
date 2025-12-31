#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

struct fold_buffer {
    unsigned char *data;
    size_t len;
    size_t cap;
};

struct fold_state {
    struct fold_buffer buf;
    size_t col;
    size_t last_blank; /* SIZE_MAX means no blank seen. */
};

enum fold_error {
    FOLD_OK = 0,
    FOLD_READ,
    FOLD_WRITE,
    FOLD_MEMORY,
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

static int buffer_reserve(struct fold_buffer *buf, size_t needed)
{
    if (buf->cap >= needed) {
        return 0;
    }
    size_t cap = buf->cap == 0 ? 256 : buf->cap;
    while (cap < needed) {
        if (cap > SIZE_MAX / 2) {
            errno = ENOMEM;
            return -1;
        }
        cap *= 2;
    }
    unsigned char *next = (unsigned char *)realloc(buf->data, cap);
    if (next == NULL) {
        errno = ENOMEM;
        return -1;
    }
    buf->data = next;
    buf->cap = cap;
    return 0;
}

static int buffer_push(struct fold_buffer *buf, unsigned char c)
{
    if (buf->len == SIZE_MAX) {
        errno = ENOMEM;
        return -1;
    }
    size_t needed = buf->len + 1;
    if (needed > buf->cap) {
        if (buffer_reserve(buf, needed) != 0) {
            return -1;
        }
    }
    buf->data[buf->len++] = c;
    return 0;
}

static size_t next_column(size_t col, unsigned char c, bool count_bytes)
{
    /* TODO: If Magnolia adds multibyte locales, fold should account for display width. */
    if (!count_bytes && c == '\b') {
        return col > 0 ? col - 1 : 0;
    }
    if (col == SIZE_MAX) {
        /* Saturate to avoid size_t wrap on very long lines. */
        return SIZE_MAX;
    }
    if (count_bytes) {
        return col + 1;
    }
    if (c == '\t') {
        /* BSD fold uses tab stops every 8 columns. */
        size_t add = 8 - (col % 8);
        if (SIZE_MAX - col < add) {
            return SIZE_MAX;
        }
        size_t next = col + add;
        return next;
    }
    return col + 1;
}

static void recompute_state(struct fold_state *st, bool count_bytes, bool break_spaces)
{
    st->col = 0;
    st->last_blank = SIZE_MAX;
    for (size_t i = 0; i < st->buf.len; ++i) {
        unsigned char c = st->buf.data[i];
        st->col = next_column(st->col, c, count_bytes);
        if (break_spaces && (c == ' ' || c == '\t')) {
            st->last_blank = i;
        }
    }
}

static int emit_line(const unsigned char *data, size_t len, bool newline)
{
    if (len > 0) {
        if (write_all(STDOUT_FILENO, data, len) != 0) {
            return -1;
        }
    }
    if (newline) {
        if (write_all(STDOUT_FILENO, "\n", 1) != 0) {
            return -1;
        }
    }
    return 0;
}

static int fold_fd(int fd,
                   const char *name,
                   bool count_bytes,
                   bool break_spaces,
                   size_t width,
                   enum fold_error *err)
{
    (void)name;
    unsigned char buf[4096];
    struct fold_state st = { 0 };

    while (1) {
        ssize_t r = read_retry(fd, buf, sizeof(buf));
        if (r < 0) {
            *err = FOLD_READ;
            goto out;
        }
        if (r == 0) {
            break;
        }
        for (ssize_t i = 0; i < r; ++i) {
            unsigned char c = buf[i];
            if (c == '\n') {
                if (emit_line(st.buf.data, st.buf.len, true) != 0) {
                    *err = FOLD_WRITE;
                    goto out;
                }
                st.buf.len = 0;
                st.col = 0;
                st.last_blank = SIZE_MAX;
                continue;
            }

            while (1) {
                size_t next = next_column(st.col, c, count_bytes);
                if (st.col > 0 && next > width) {
                    if (break_spaces && st.last_blank != SIZE_MAX) {
                        /* With -s, fold at the last blank and drop it. */
                        size_t break_at = st.last_blank;
                        if (emit_line(st.buf.data, break_at, true) != 0) {
                            *err = FOLD_WRITE;
                            goto out;
                        }
                        size_t remain = st.buf.len - (break_at + 1);
                        if (remain > 0) {
                            memmove(st.buf.data, st.buf.data + break_at + 1, remain);
                        }
                        st.buf.len = remain;
                        recompute_state(&st, count_bytes, break_spaces);
                        continue;
                    }
                    if (emit_line(st.buf.data, st.buf.len, true) != 0) {
                        *err = FOLD_WRITE;
                        goto out;
                    }
                    st.buf.len = 0;
                    st.col = 0;
                    st.last_blank = SIZE_MAX;
                }

                if (buffer_push(&st.buf, c) != 0) {
                    *err = FOLD_MEMORY;
                    goto out;
                }
                st.col = next_column(st.col, c, count_bytes);
                if (break_spaces && (c == ' ' || c == '\t')) {
                    st.last_blank = st.buf.len - 1;
                }
                break;
            }
        }
    }

    if (st.buf.len > 0) {
        if (emit_line(st.buf.data, st.buf.len, false) != 0) {
            *err = FOLD_WRITE;
            goto out;
        }
    }

    *err = FOLD_OK;

out:
    free(st.buf.data);
    return *err == FOLD_OK ? 0 : -1;
}

static void usage(void)
{
    eprintf("usage: fold [-bs] [-w width] [file ...]\n");
}

static int parse_width(const char *value, size_t *out)
{
    if (value == NULL || *value == '\0') {
        return -1;
    }
    errno = 0;
    char *end = NULL;
    unsigned long long v = strtoull(value, &end, 10);
    if (errno != 0 || end == value || *end != '\0') {
        return -1;
    }
    if (v == 0 || v > SIZE_MAX) {
        return -1;
    }
    *out = (size_t)v;
    return 0;
}

int main(int argc, char **argv)
{
    bool count_bytes = false;
    bool break_spaces = false;
    size_t width = 80;

    opterr = 0;
    int ch;
    while ((ch = getopt(argc, argv, "bsw:")) != -1) {
        switch (ch) {
        case 'b':
            count_bytes = true;
            break;
        case 's':
            break_spaces = true;
            break;
        case 'w':
            if (parse_width(optarg, &width) != 0) {
                eprintf("fold: illegal width -- %s\n", optarg);
                usage();
                return 1;
            }
            break;
        case '?':
        default:
            if (optopt == 'w') {
                eprintf("fold: option requires an argument -- %c\n", optopt);
            } else if (optopt != 0) {
                eprintf("fold: illegal option -- %c\n", optopt);
            } else {
                eprintf("fold: illegal option\n");
            }
            usage();
            return 1;
        }
    }

    if (width == 0) {
        eprintf("fold: illegal width -- 0\n");
        usage();
        return 1;
    }

    int status = 0;
    if (optind >= argc) {
        enum fold_error err = FOLD_OK;
        if (fold_fd(STDIN_FILENO, "stdin", count_bytes, break_spaces, width, &err) != 0) {
            if (err == FOLD_WRITE) {
                eprintf("fold: stdout: %s\n", strerror(errno));
            } else if (err == FOLD_READ) {
                eprintf("fold: stdin: %s\n", strerror(errno));
            } else if (err == FOLD_MEMORY) {
                eprintf("fold: out of memory\n");
            }
            status = 1;
        }
        return status;
    }

    for (int i = optind; i < argc; ++i) {
        const char *path = argv[i];
        enum fold_error err = FOLD_OK;
        if (strcmp(path, "-") == 0) {
            if (fold_fd(STDIN_FILENO, path, count_bytes, break_spaces, width, &err) != 0) {
                if (err == FOLD_WRITE) {
                    eprintf("fold: stdout: %s\n", strerror(errno));
                } else if (err == FOLD_READ) {
                    eprintf("fold: %s: %s\n", path, strerror(errno));
                } else if (err == FOLD_MEMORY) {
                    eprintf("fold: out of memory\n");
                }
                status = 1;
            }
            continue;
        }
        int fd = open(path, O_RDONLY);
        if (fd < 0) {
            eprintf("fold: %s: %s\n", path, strerror(errno));
            status = 1;
            continue;
        }
        if (fold_fd(fd, path, count_bytes, break_spaces, width, &err) != 0) {
            if (err == FOLD_WRITE) {
                eprintf("fold: stdout: %s\n", strerror(errno));
            } else if (err == FOLD_READ) {
                eprintf("fold: %s: %s\n", path, strerror(errno));
            } else if (err == FOLD_MEMORY) {
                eprintf("fold: out of memory\n");
            }
            status = 1;
        }
        close(fd);
    }

    return status;
}
