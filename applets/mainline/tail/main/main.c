#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdbool.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

enum header_mode {
    HEADER_AUTO = 0,
    HEADER_QUIET,
    HEADER_VERBOSE,
};

enum count_mode {
    COUNT_LINES = 0,
    COUNT_BYTES,
};

enum count_origin {
    ORIGIN_END = 0,
    ORIGIN_START,
};

enum io_error {
    IO_OK = 0,
    IO_READ,
    IO_WRITE,
};

struct count_spec {
    size_t count;
    enum count_origin origin;
};

struct tail_spec {
    size_t count;
    enum count_mode mode;
    enum count_origin origin;
    bool follow;
    bool follow_name;
    bool reverse;
    enum header_mode header;
};

struct line_buf {
    char *data;
    size_t len;
};

struct line_ring {
    struct line_buf *lines;
    size_t cap;
    size_t count;
    size_t next;
};

struct line_vec {
    struct line_buf *lines;
    size_t len;
    size_t cap;
};

struct line_accum {
    char *data;
    size_t len;
    size_t cap;
};

struct file_state {
    const char *path;
    int fd;
    bool is_stdin;
    bool regular;
    dev_t dev;
    ino_t ino;
    off_t offset;
    unsigned char *pending;
    size_t pending_len;
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

static int read_full(int fd, void *buf, size_t len)
{
    unsigned char *p = (unsigned char *)buf;
    size_t off = 0;
    while (off < len) {
        ssize_t r = read_retry(fd, p + off, len - off);
        if (r <= 0) {
            return -1;
        }
        off += (size_t)r;
    }
    return 0;
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

static void sleep_one(void)
{
    struct timespec ts;
    ts.tv_sec = 1;
    ts.tv_nsec = 0;
    while (nanosleep(&ts, &ts) != 0) {
        if (errno != EINTR) {
            break;
        }
    }
}

static void set_nonblocking(int fd, bool enable)
{
    int flags = fcntl(fd, F_GETFL);
    if (flags < 0) {
        return;
    }
    if (enable) {
        flags |= O_NONBLOCK;
    } else {
        flags &= ~O_NONBLOCK;
    }
    (void)fcntl(fd, F_SETFL, flags);
}

static int parse_count_spec(const char *s, struct count_spec *out)
{
    if (!s || *s == '\0') {
        return -1;
    }
    enum count_origin origin = ORIGIN_END;
    if (*s == '+' || *s == '-') {
        origin = (*s == '+') ? ORIGIN_START : ORIGIN_END;
        s++;
    }
    if (*s == '\0') {
        return -1;
    }
    errno = 0;
    char *end = NULL;
    unsigned long long v = strtoull(s, &end, 10);
    if (errno == ERANGE || end == s || *end != '\0') {
        return -1;
    }
    if (v > SIZE_MAX) {
        return -1;
    }
    out->count = (size_t)v;
    out->origin = origin;
    return 0;
}

static int parse_legacy_spec(const char *arg, struct count_spec *out, enum count_mode *mode, bool *follow)
{
    if (!arg || (*arg != '+' && *arg != '-')) {
        return -1;
    }
    enum count_origin origin = (*arg == '+') ? ORIGIN_START : ORIGIN_END;
    const char *p = arg + 1;
    if (!isdigit((unsigned char)*p)) {
        return -1;
    }
    errno = 0;
    unsigned long long v = 0;
    while (isdigit((unsigned char)*p)) {
        unsigned digit = (unsigned)(*p - '0');
        if (v > (ULLONG_MAX - digit) / 10ULL) {
            errno = ERANGE;
            break;
        }
        v = v * 10ULL + digit;
        p++;
    }
    if (errno == ERANGE || v > SIZE_MAX) {
        return -1;
    }

    enum count_mode local_mode = COUNT_LINES;
    bool mode_set = false;
    bool local_follow = false;

    for (; *p != '\0'; ++p) {
        if (*p == 'c') {
            if (mode_set) {
                return -1;
            }
            local_mode = COUNT_BYTES;
            mode_set = true;
        } else if (*p == 'l') {
            if (mode_set) {
                return -1;
            }
            local_mode = COUNT_LINES;
            mode_set = true;
        } else if (*p == 'f') {
            local_follow = true;
        } else {
            return -1;
        }
    }

    out->count = (size_t)v;
    out->origin = origin;
    *mode = local_mode;
    *follow = local_follow;
    return 0;
}

static void normalize_count(struct count_spec *spec)
{
    if (spec->origin == ORIGIN_START && spec->count == 0) {
        /* BSD historical behavior: +0 is treated as +1. */
        spec->count = 1;
    }
}

static int line_accum_append(struct line_accum *accum, const char *data, size_t len)
{
    if (len == 0) {
        return 0;
    }
    if (accum->len + len < accum->len) {
        errno = EOVERFLOW;
        return -1;
    }
    size_t needed = accum->len + len;
    if (needed > accum->cap) {
        size_t next = accum->cap ? accum->cap : 128;
        while (next < needed) {
            if (next > SIZE_MAX / 2) {
                errno = EOVERFLOW;
                return -1;
            }
            next *= 2;
        }
        char *tmp = (char *)realloc(accum->data, next);
        if (!tmp) {
            errno = ENOMEM;
            return -1;
        }
        accum->data = tmp;
        accum->cap = next;
    }
    memcpy(accum->data + accum->len, data, len);
    accum->len += len;
    return 0;
}

static void line_accum_free(struct line_accum *accum)
{
    free(accum->data);
    accum->data = NULL;
    accum->len = 0;
    accum->cap = 0;
}

static int line_ring_init(struct line_ring *ring, size_t cap)
{
    ring->lines = NULL;
    ring->cap = cap;
    ring->count = 0;
    ring->next = 0;
    if (cap == 0) {
        return 0;
    }
    if (cap > SIZE_MAX / sizeof(*ring->lines)) {
        errno = EOVERFLOW;
        return -1;
    }
    ring->lines = (struct line_buf *)calloc(cap, sizeof(*ring->lines));
    if (!ring->lines) {
        errno = ENOMEM;
        return -1;
    }
    return 0;
}

static void line_ring_free(struct line_ring *ring)
{
    if (!ring->lines) {
        return;
    }
    for (size_t i = 0; i < ring->cap; ++i) {
        free(ring->lines[i].data);
    }
    free(ring->lines);
    ring->lines = NULL;
    ring->cap = 0;
    ring->count = 0;
    ring->next = 0;
}

static int line_ring_push(struct line_ring *ring, const char *data, size_t len)
{
    if (ring->cap == 0) {
        return 0;
    }
    if (ring->count == ring->cap) {
        free(ring->lines[ring->next].data);
        ring->lines[ring->next].data = NULL;
        ring->lines[ring->next].len = 0;
    } else {
        ring->count++;
    }
    char *copy = NULL;
    if (len > 0) {
        copy = (char *)malloc(len);
        if (!copy) {
            errno = ENOMEM;
            return -1;
        }
        memcpy(copy, data, len);
    }
    ring->lines[ring->next].data = copy;
    ring->lines[ring->next].len = len;
    ring->next = (ring->next + 1) % ring->cap;
    return 0;
}

static int line_ring_write(const struct line_ring *ring, bool reverse, enum io_error *err)
{
    if (ring->count == 0) {
        return 0;
    }
    size_t start = (ring->count < ring->cap) ? 0 : ring->next;
    for (size_t i = 0; i < ring->count; ++i) {
        size_t idx = reverse
            ? (start + ring->count - 1 - i) % ring->cap
            : (start + i) % ring->cap;
        if (ring->lines[idx].len == 0) {
            continue;
        }
        if (write_all(STDOUT_FILENO, ring->lines[idx].data, ring->lines[idx].len) != 0) {
            *err = IO_WRITE;
            return -1;
        }
    }
    return 0;
}

static int line_vec_push(struct line_vec *vec, const char *data, size_t len)
{
    if (vec->len == vec->cap) {
        size_t next = vec->cap ? vec->cap * 2 : 64;
        if (next < vec->cap || next > SIZE_MAX / sizeof(*vec->lines)) {
            errno = EOVERFLOW;
            return -1;
        }
        struct line_buf *tmp = (struct line_buf *)realloc(vec->lines, next * sizeof(*vec->lines));
        if (!tmp) {
            errno = ENOMEM;
            return -1;
        }
        vec->lines = tmp;
        vec->cap = next;
    }
    char *copy = NULL;
    if (len > 0) {
        copy = (char *)malloc(len);
        if (!copy) {
            errno = ENOMEM;
            return -1;
        }
        memcpy(copy, data, len);
    }
    vec->lines[vec->len].data = copy;
    vec->lines[vec->len].len = len;
    vec->len++;
    return 0;
}

static void line_vec_free(struct line_vec *vec)
{
    for (size_t i = 0; i < vec->len; ++i) {
        free(vec->lines[i].data);
    }
    free(vec->lines);
    vec->lines = NULL;
    vec->len = 0;
    vec->cap = 0;
}

static void clear_pending(struct file_state *st)
{
    free(st->pending);
    st->pending = NULL;
    st->pending_len = 0;
}

static int line_vec_write_reverse(const struct line_vec *vec, enum io_error *err)
{
    for (size_t i = vec->len; i > 0; --i) {
        const struct line_buf *line = &vec->lines[i - 1];
        if (line->len == 0) {
            continue;
        }
        if (write_all(STDOUT_FILENO, line->data, line->len) != 0) {
            *err = IO_WRITE;
            return -1;
        }
    }
    return 0;
}

static int copy_all(int fd, enum io_error *err)
{
    char buf[4096];
    while (1) {
        ssize_t r = read_retry(fd, buf, sizeof(buf));
        if (r < 0) {
            *err = IO_READ;
            return -1;
        }
        if (r == 0) {
            return 0;
        }
        if (write_all(STDOUT_FILENO, buf, (size_t)r) != 0) {
            *err = IO_WRITE;
            return -1;
        }
    }
}

static int copy_from_offset(int fd, off_t *offset, enum io_error *err)
{
    if (lseek(fd, *offset, SEEK_SET) < 0) {
        *err = IO_READ;
        return -1;
    }
    char buf[4096];
    while (1) {
        ssize_t r = read_retry(fd, buf, sizeof(buf));
        if (r < 0) {
            *err = IO_READ;
            return -1;
        }
        if (r == 0) {
            return 0;
        }
        if (write_all(STDOUT_FILENO, buf, (size_t)r) != 0) {
            *err = IO_WRITE;
            return -1;
        }
        *offset += (off_t)r;
    }
}

static int tail_bytes_end_regular(int fd, size_t count, off_t size, enum io_error *err)
{
    if (count == 0) {
        return 0;
    }
    off_t start = 0;
    if (size > 0) {
        uintmax_t usize = (uintmax_t)size;
        if ((uintmax_t)count < usize) {
            start = size - (off_t)count;
        }
    }
    if (lseek(fd, start, SEEK_SET) < 0) {
        *err = IO_READ;
        return -1;
    }
    return copy_all(fd, err);
}

static int tail_bytes_start_regular(int fd, size_t count, off_t size, enum io_error *err)
{
    if (count == 0) {
        return 0;
    }
    uintmax_t start_u = (uintmax_t)(count - 1);
    if (start_u >= (uintmax_t)size) {
        return 0;
    }
    off_t start = (off_t)start_u;
    if (lseek(fd, start, SEEK_SET) < 0) {
        *err = IO_READ;
        return -1;
    }
    return copy_all(fd, err);
}

static int tail_bytes_end_stream(int fd, size_t count, enum io_error *err)
{
    if (count == 0) {
        return 0;
    }
    unsigned char *ring = (unsigned char *)malloc(count);
    if (!ring) {
        errno = ENOMEM;
        *err = IO_READ;
        return -1;
    }
    size_t pos = 0;
    size_t total = 0;
    char buf[4096];
    while (1) {
        ssize_t r = read_retry(fd, buf, sizeof(buf));
        if (r < 0) {
            free(ring);
            *err = IO_READ;
            return -1;
        }
        if (r == 0) {
            break;
        }
        for (ssize_t i = 0; i < r; ++i) {
            ring[pos] = (unsigned char)buf[i];
            pos = (pos + 1) % count;
            total++;
        }
    }
    size_t out_len = (total < count) ? total : count;
    if (out_len > 0) {
        if (total >= count) {
            if (write_all(STDOUT_FILENO, ring + pos, count - pos) != 0) {
                free(ring);
                *err = IO_WRITE;
                return -1;
            }
            if (pos > 0 && write_all(STDOUT_FILENO, ring, pos) != 0) {
                free(ring);
                *err = IO_WRITE;
                return -1;
            }
        } else {
            if (write_all(STDOUT_FILENO, ring, out_len) != 0) {
                free(ring);
                *err = IO_WRITE;
                return -1;
            }
        }
    }
    free(ring);
    return 0;
}

static int tail_bytes_start_stream(int fd, size_t count, enum io_error *err)
{
    if (count == 0) {
        return 0;
    }
    size_t skip = count - 1;
    char buf[4096];
    while (1) {
        ssize_t r = read_retry(fd, buf, sizeof(buf));
        if (r < 0) {
            *err = IO_READ;
            return -1;
        }
        if (r == 0) {
            return 0;
        }
        size_t off = 0;
        if (skip > 0) {
            size_t consume = (size_t)r;
            if (consume <= skip) {
                skip -= consume;
                continue;
            }
            off = skip;
            skip = 0;
        }
        if (off < (size_t)r) {
            if (write_all(STDOUT_FILENO, buf + off, (size_t)r - off) != 0) {
                *err = IO_WRITE;
                return -1;
            }
        }
    }
}

static int skip_bytes_to_start(int fd, size_t count, struct file_state *st, enum io_error *err)
{
    clear_pending(st);
    if (count <= 1) {
        return 0;
    }
    size_t skip = count - 1;
    char buf[4096];
    while (1) {
        ssize_t r = read_retry(fd, buf, sizeof(buf));
        if (r < 0) {
            *err = IO_READ;
            return -1;
        }
        if (r == 0) {
            return 0;
        }
        if ((size_t)r <= skip) {
            skip -= (size_t)r;
            continue;
        }
        size_t remain = (size_t)r - skip;
        unsigned char *pending = (unsigned char *)malloc(remain);
        if (!pending) {
            errno = ENOMEM;
            *err = IO_READ;
            return -1;
        }
        memcpy(pending, buf + skip, remain);
        st->pending = pending;
        st->pending_len = remain;
        return 0;
    }
}

static int skip_lines_to_start(int fd, size_t start_line, struct file_state *st, enum io_error *err)
{
    clear_pending(st);
    if (start_line <= 1) {
        return 0;
    }
    size_t line = 1;
    char buf[4096];
    while (1) {
        ssize_t r = read_retry(fd, buf, sizeof(buf));
        if (r < 0) {
            *err = IO_READ;
            return -1;
        }
        if (r == 0) {
            return 0;
        }
        size_t off = 0;
        while (off < (size_t)r) {
            if (buf[off] == '\n') {
                line++;
                if (line >= start_line) {
                    off++;
                    size_t remain = (size_t)r - off;
                    if (remain > 0) {
                        unsigned char *pending = (unsigned char *)malloc(remain);
                        if (!pending) {
                            errno = ENOMEM;
                            *err = IO_READ;
                            return -1;
                        }
                        memcpy(pending, buf + off, remain);
                        st->pending = pending;
                        st->pending_len = remain;
                    }
                    return 0;
                }
            }
            off++;
        }
    }
}

static int tail_lines_end_stream_ring(int fd, size_t count, bool reverse, enum io_error *err)
{
    if (count == 0) {
        return 0;
    }
    struct line_ring ring;
    if (line_ring_init(&ring, count) != 0) {
        *err = IO_READ;
        return -1;
    }
    struct line_accum accum = {0};
    char buf[4096];
    while (1) {
        ssize_t r = read_retry(fd, buf, sizeof(buf));
        if (r < 0) {
            line_ring_free(&ring);
            line_accum_free(&accum);
            *err = IO_READ;
            return -1;
        }
        if (r == 0) {
            break;
        }
        size_t off = 0;
        while (off < (size_t)r) {
            void *nl = memchr(buf + off, '\n', (size_t)r - off);
            size_t chunk = nl ? (size_t)((char *)nl - (buf + off) + 1) : (size_t)r - off;
            if (line_accum_append(&accum, buf + off, chunk) != 0) {
                line_ring_free(&ring);
                line_accum_free(&accum);
                *err = IO_READ;
                return -1;
            }
            off += chunk;
            if (nl) {
                if (line_ring_push(&ring, accum.data, accum.len) != 0) {
                    line_ring_free(&ring);
                    line_accum_free(&accum);
                    *err = IO_READ;
                    return -1;
                }
                accum.len = 0;
            }
        }
    }
    if (accum.len > 0) {
        if (line_ring_push(&ring, accum.data, accum.len) != 0) {
            line_ring_free(&ring);
            line_accum_free(&accum);
            *err = IO_READ;
            return -1;
        }
    }
    line_accum_free(&accum);
    int rc = line_ring_write(&ring, reverse, err);
    line_ring_free(&ring);
    return rc;
}

static int tail_lines_start_stream(int fd, size_t start_line, enum io_error *err)
{
    if (start_line <= 1) {
        return copy_all(fd, err);
    }
    size_t line = 1;
    bool output = false;
    char buf[4096];
    while (1) {
        ssize_t r = read_retry(fd, buf, sizeof(buf));
        if (r < 0) {
            *err = IO_READ;
            return -1;
        }
        if (r == 0) {
            return 0;
        }
        size_t off = 0;
        if (!output) {
            while (off < (size_t)r) {
                if (buf[off] == '\n') {
                    line++;
                    if (line >= start_line) {
                        output = true;
                        off++;
                        break;
                    }
                }
                off++;
            }
            if (!output) {
                continue;
            }
        }
        if (off < (size_t)r) {
            if (write_all(STDOUT_FILENO, buf + off, (size_t)r - off) != 0) {
                *err = IO_WRITE;
                return -1;
            }
        }
    }
}

static int tail_lines_start_reverse(int fd, size_t start_line, enum io_error *err)
{
    struct line_vec vec = {0};
    struct line_accum accum = {0};
    size_t line = 1;

    char buf[4096];
    while (1) {
        ssize_t r = read_retry(fd, buf, sizeof(buf));
        if (r < 0) {
            line_vec_free(&vec);
            line_accum_free(&accum);
            *err = IO_READ;
            return -1;
        }
        if (r == 0) {
            break;
        }
        size_t off = 0;
        while (off < (size_t)r) {
            void *nl = memchr(buf + off, '\n', (size_t)r - off);
            size_t chunk = nl ? (size_t)((char *)nl - (buf + off) + 1) : (size_t)r - off;
            if (line_accum_append(&accum, buf + off, chunk) != 0) {
                line_vec_free(&vec);
                line_accum_free(&accum);
                *err = IO_READ;
                return -1;
            }
            off += chunk;
            if (nl) {
                if (line >= start_line) {
                    if (line_vec_push(&vec, accum.data, accum.len) != 0) {
                        line_vec_free(&vec);
                        line_accum_free(&accum);
                        *err = IO_READ;
                        return -1;
                    }
                }
                accum.len = 0;
                line++;
            }
        }
    }
    if (accum.len > 0 && line >= start_line) {
        if (line_vec_push(&vec, accum.data, accum.len) != 0) {
            line_vec_free(&vec);
            line_accum_free(&accum);
            *err = IO_READ;
            return -1;
        }
    }
    line_accum_free(&accum);
    int rc = line_vec_write_reverse(&vec, err);
    line_vec_free(&vec);
    return rc;
}

static int compute_tail_start_offset(int fd, size_t count, off_t *start, enum io_error *err)
{
    struct stat sb;
    if (fstat(fd, &sb) != 0) {
        *err = IO_READ;
        return -1;
    }
    off_t size = sb.st_size;
    if (count == 0 || size == 0) {
        *start = size;
        return 0;
    }

    if (lseek(fd, size - 1, SEEK_SET) < 0) {
        *err = IO_READ;
        return -1;
    }
    char last = '\0';
    if (read_retry(fd, &last, 1) != 1) {
        *err = IO_READ;
        return -1;
    }
    bool ends_with_nl = (last == '\n');
    size_t target = count;
    if (ends_with_nl) {
        if (count != SIZE_MAX) {
            target = count + 1;
        }
    }

    size_t lines = 0;
    off_t pos = size;
    char buf[4096];
    while (pos > 0) {
        size_t chunk = (pos >= (off_t)sizeof(buf)) ? sizeof(buf) : (size_t)pos;
        pos -= (off_t)chunk;
        if (lseek(fd, pos, SEEK_SET) < 0) {
            *err = IO_READ;
            return -1;
        }
        if (read_full(fd, buf, chunk) != 0) {
            *err = IO_READ;
            return -1;
        }
        for (size_t i = chunk; i > 0; --i) {
            if (buf[i - 1] == '\n') {
                lines++;
                if (lines >= target) {
                    *start = pos + (off_t)i;
                    return 0;
                }
            }
        }
    }
    *start = 0;
    return 0;
}

static int tail_lines_end_regular_forward(int fd, size_t count, enum io_error *err)
{
    off_t start = 0;
    if (compute_tail_start_offset(fd, count, &start, err) != 0) {
        return -1;
    }
    if (lseek(fd, start, SEEK_SET) < 0) {
        *err = IO_READ;
        return -1;
    }
    return copy_all(fd, err);
}

static int tail_fd(int fd, const struct tail_spec *spec, enum io_error *err)
{
    *err = IO_OK;
    struct stat sb;
    bool regular = (fstat(fd, &sb) == 0) && S_ISREG(sb.st_mode);

    if (spec->mode == COUNT_BYTES) {
        if (spec->origin == ORIGIN_END) {
            if (regular) {
                return tail_bytes_end_regular(fd, spec->count, sb.st_size, err);
            }
            return tail_bytes_end_stream(fd, spec->count, err);
        }
        if (regular) {
            return tail_bytes_start_regular(fd, spec->count, sb.st_size, err);
        }
        return tail_bytes_start_stream(fd, spec->count, err);
    }

    if (spec->reverse) {
        if (spec->origin == ORIGIN_END) {
            return tail_lines_end_stream_ring(fd, spec->count, true, err);
        }
        return tail_lines_start_reverse(fd, spec->count, err);
    }

    if (spec->origin == ORIGIN_END && regular) {
        return tail_lines_end_regular_forward(fd, spec->count, err);
    }
    if (spec->origin == ORIGIN_END) {
        return tail_lines_end_stream_ring(fd, spec->count, false, err);
    }
    return tail_lines_start_stream(fd, spec->count, err);
}

static int print_header(const char *path, bool *need_separator)
{
    if (*need_separator) {
        if (write_all(STDOUT_FILENO, "\n", 1) != 0) {
            return -1;
        }
    }
    if (write_all(STDOUT_FILENO, "==> ", 4) != 0) {
        return -1;
    }
    if (write_all(STDOUT_FILENO, path, strlen(path)) != 0) {
        return -1;
    }
    if (write_all(STDOUT_FILENO, " <==\n", 5) != 0) {
        return -1;
    }
    *need_separator = true;
    return 0;
}

static bool should_show_header(const struct tail_spec *spec, int file_count)
{
    if (spec->header == HEADER_VERBOSE) {
        return true;
    }
    return spec->header == HEADER_AUTO && file_count > 1;
}

static int maybe_print_header(const char *path, bool show_header, int idx, int *last_output, bool *need_separator)
{
    if (!show_header || *last_output == idx) {
        return 0;
    }
    if (print_header(path, need_separator) != 0) {
        return -1;
    }
    *last_output = idx;
    return 0;
}

static void usage(void)
{
    eprintf("usage: tail [-F | -f | -r] [-qv] [-n number | -c number] [file ...]\n");
}

static int open_file_state(struct file_state *st, const char *path)
{
    clear_pending(st);
    st->path = path;
    st->is_stdin = (strcmp(path, "-") == 0);
    st->fd = STDIN_FILENO;
    st->regular = false;
    st->dev = 0;
    st->ino = 0;
    st->offset = 0;

    if (!st->is_stdin) {
        st->fd = open(path, O_RDONLY);
        if (st->fd < 0) {
            st->fd = -1;
            return -1;
        }
    }

    struct stat sb;
    if (fstat(st->fd, &sb) != 0) {
        if (!st->is_stdin) {
            (void)close(st->fd);
        }
        st->fd = -1;
        return -1;
    }
    st->regular = S_ISREG(sb.st_mode);
    st->dev = sb.st_dev;
    st->ino = sb.st_ino;
    st->offset = sb.st_size;
    return 0;
}

static void close_file_states(struct file_state *files, int file_count)
{
    for (int i = 0; i < file_count; ++i) {
        if (files[i].fd >= 0 && !files[i].is_stdin) {
            (void)close(files[i].fd);
        }
        clear_pending(&files[i]);
        files[i].fd = -1;
    }
}

static int follow_files(const struct tail_spec *spec, struct file_state *files, int file_count, bool *need_separator, int *last_output)
{
    const bool show_header = should_show_header(spec, file_count);
    const bool use_nonblocking = file_count > 1;

    for (int i = 0; i < file_count; ++i) {
        if (use_nonblocking && files[i].fd >= 0 && !files[i].regular) {
            set_nonblocking(files[i].fd, true);
        }
    }

    while (1) {
        bool had_output = false;
        for (int i = 0; i < file_count; ++i) {
            struct file_state *st = &files[i];

            if (st->fd < 0) {
                if (spec->follow_name && !st->is_stdin) {
                    if (open_file_state(st, st->path) != 0) {
                        continue;
                    }
                    if (!st->regular) {
                        if (use_nonblocking) {
                            set_nonblocking(st->fd, true);
                        }
                        enum io_error err = IO_OK;
                        if (spec->origin == ORIGIN_START && spec->count > 1) {
                            int rc = (spec->mode == COUNT_BYTES)
                                ? skip_bytes_to_start(st->fd, spec->count, st, &err)
                                : skip_lines_to_start(st->fd, spec->count, st, &err);
                            if (rc != 0) {
                                eprintf("tail: %s: %s\n", st->path, strerror(errno));
                                return 1;
                            }
                        }
                        if (st->pending_len > 0) {
                            if (maybe_print_header(st->path, show_header, i, last_output, need_separator) != 0) {
                                eprintf("tail: stdout: %s\n", strerror(errno));
                                return 1;
                            }
                            if (write_all(STDOUT_FILENO, st->pending, st->pending_len) != 0) {
                                eprintf("tail: stdout: %s\n", strerror(errno));
                                return 1;
                            }
                            clear_pending(st);
                            *last_output = i;
                            had_output = true;
                        }
                        continue;
                    }

                    if (maybe_print_header(st->path, show_header, i, last_output, need_separator) != 0) {
                        eprintf("tail: stdout: %s\n", strerror(errno));
                        return 1;
                    }
                    enum io_error err = IO_OK;
                    if (tail_fd(st->fd, spec, &err) != 0) {
                        if (err == IO_WRITE) {
                            eprintf("tail: stdout: %s\n", strerror(errno));
                            return 1;
                        }
                        eprintf("tail: %s: %s\n", st->path, strerror(errno));
                        return 1;
                    }
                    struct stat sb;
                    if (fstat(st->fd, &sb) != 0) {
                        eprintf("tail: %s: %s\n", st->path, strerror(errno));
                        return 1;
                    }
                    st->offset = sb.st_size;
                    *last_output = i;
                    had_output = true;
                }
                continue;
            }

            if (spec->follow_name && !st->is_stdin) {
                struct stat sb;
                if (stat(st->path, &sb) == 0) {
                    if (sb.st_ino != st->ino || sb.st_dev != st->dev) {
                        if (!st->is_stdin) {
                            (void)close(st->fd);
                        }
                        st->fd = -1;
                        st->regular = false;
                        clear_pending(st);
                        if (*last_output == i) {
                            *last_output = -1;
                        }
                        continue;
                    }
                }
            }

            if (st->pending_len > 0) {
                if (maybe_print_header(st->path, show_header, i, last_output, need_separator) != 0) {
                    eprintf("tail: stdout: %s\n", strerror(errno));
                    return 1;
                }
                if (write_all(STDOUT_FILENO, st->pending, st->pending_len) != 0) {
                    eprintf("tail: stdout: %s\n", strerror(errno));
                    return 1;
                }
                clear_pending(st);
                *last_output = i;
                had_output = true;
            }

            if (st->regular) {
                struct stat sb;
                if (fstat(st->fd, &sb) != 0) {
                    eprintf("tail: %s: %s\n", st->path, strerror(errno));
                    return 1;
                }
                if (sb.st_size < st->offset) {
                    st->offset = 0;
                }
                if (sb.st_size > st->offset) {
                    if (maybe_print_header(st->path, show_header, i, last_output, need_separator) != 0) {
                        eprintf("tail: stdout: %s\n", strerror(errno));
                        return 1;
                    }
                    enum io_error err = IO_OK;
                    if (copy_from_offset(st->fd, &st->offset, &err) != 0) {
                        if (err == IO_WRITE) {
                            eprintf("tail: stdout: %s\n", strerror(errno));
                            return 1;
                        }
                        eprintf("tail: %s: %s\n", st->path, strerror(errno));
                        return 1;
                    }
                    *last_output = i;
                    had_output = true;
                }
                continue;
            }

            char buf[4096];
            while (1) {
                ssize_t r = read_retry(st->fd, buf, sizeof(buf));
                if (r < 0) {
                    if (errno == EAGAIN || errno == EWOULDBLOCK) {
                        break;
                    }
                    eprintf("tail: %s: %s\n", st->path, strerror(errno));
                    return 1;
                }
                if (r == 0) {
                    break;
                }
                if (maybe_print_header(st->path, show_header, i, last_output, need_separator) != 0) {
                    eprintf("tail: stdout: %s\n", strerror(errno));
                    return 1;
                }
                if (write_all(STDOUT_FILENO, buf, (size_t)r) != 0) {
                    eprintf("tail: stdout: %s\n", strerror(errno));
                    return 1;
                }
                *last_output = i;
                had_output = true;
            }
        }
        if (!had_output) {
            sleep_one();
        }
    }
}

int main(int argc, char **argv)
{
    struct tail_spec spec;
    spec.count = 10;
    spec.mode = COUNT_LINES;
    spec.origin = ORIGIN_END;
    spec.follow = false;
    spec.follow_name = false;
    spec.reverse = false;
    spec.header = HEADER_AUTO;

    bool seen_lines = false;
    bool seen_bytes = false;
    bool seen_f = false;
    bool seen_F = false;
    bool seen_r = false;

    int file_index = argc;
    for (int i = 1; i < argc; ++i) {
        const char *arg = argv[i];
        if (!arg) {
            continue;
        }
        if (strcmp(arg, "--") == 0) {
            file_index = i + 1;
            break;
        }
        if (arg[0] == '+' && isdigit((unsigned char)arg[1])) {
            struct count_spec count;
            enum count_mode mode = COUNT_LINES;
            bool legacy_follow = false;
            if (parse_legacy_spec(arg, &count, &mode, &legacy_follow) != 0) {
                eprintf("tail: illegal offset -- %s\n", arg);
                usage();
                return 1;
            }
            normalize_count(&count);
            if (mode == COUNT_LINES) {
                seen_lines = true;
            } else {
                seen_bytes = true;
            }
            if (seen_lines && seen_bytes) {
                usage();
                return 1;
            }
            spec.mode = mode;
            spec.count = count.count;
            spec.origin = count.origin;
            if (legacy_follow) {
                spec.follow = true;
                seen_f = true;
            }
            continue;
        }
        if (arg[0] != '-' || arg[1] == '\0') {
            file_index = i;
            break;
        }
        if (isdigit((unsigned char)arg[1])) {
            struct count_spec count;
            enum count_mode mode = COUNT_LINES;
            bool legacy_follow = false;
            if (parse_legacy_spec(arg, &count, &mode, &legacy_follow) != 0) {
                eprintf("tail: illegal offset -- %s\n", arg + 1);
                usage();
                return 1;
            }
            normalize_count(&count);
            if (mode == COUNT_LINES) {
                seen_lines = true;
            } else {
                seen_bytes = true;
            }
            if (seen_lines && seen_bytes) {
                usage();
                return 1;
            }
            spec.mode = mode;
            spec.count = count.count;
            spec.origin = count.origin;
            if (legacy_follow) {
                spec.follow = true;
                seen_f = true;
            }
            continue;
        }

        bool done = false;
        for (size_t j = 1; arg[j] != '\0'; ++j) {
            char ch = arg[j];
            switch (ch) {
            case 'F':
                spec.follow = true;
                spec.follow_name = true;
                seen_F = true;
                break;
            case 'f':
                spec.follow = true;
                seen_f = true;
                break;
            case 'r':
                spec.reverse = true;
                seen_r = true;
                break;
            case 'q':
                spec.header = HEADER_QUIET;
                break;
            case 'v':
                spec.header = HEADER_VERBOSE;
                break;
            case 'n':
            case 'c': {
                const char *value = NULL;
                if (arg[j + 1] != '\0') {
                    value = arg + j + 1;
                } else {
                    if (i + 1 >= argc) {
                        eprintf("tail: option requires an argument -- %c\n", ch);
                        usage();
                        return 1;
                    }
                    value = argv[++i];
                }
                struct count_spec count;
                if (parse_count_spec(value, &count) != 0) {
                    if (ch == 'n') {
                        eprintf("tail: illegal line count -- %s\n", value);
                    } else {
                        eprintf("tail: illegal byte count -- %s\n", value);
                    }
                    usage();
                    return 1;
                }
                normalize_count(&count);
                if (ch == 'n') {
                    seen_lines = true;
                } else {
                    seen_bytes = true;
                }
                if (seen_lines && seen_bytes) {
                    usage();
                    return 1;
                }
                spec.mode = (ch == 'n') ? COUNT_LINES : COUNT_BYTES;
                spec.count = count.count;
                spec.origin = count.origin;
                done = true;
                break;
            }
            default:
                eprintf("tail: illegal option -- %c\n", ch);
                usage();
                return 1;
            }
            if (done) {
                break;
            }
        }
    }

    if ((seen_r ? 1 : 0) + (seen_f ? 1 : 0) + (seen_F ? 1 : 0) > 1) {
        usage();
        return 1;
    }

    if (spec.reverse && spec.mode == COUNT_BYTES) {
        usage();
        return 1;
    }

    if (!spec.follow) {
        if (file_index >= argc) {
            enum io_error err = IO_OK;
            if (tail_fd(STDIN_FILENO, &spec, &err) != 0) {
                if (err == IO_WRITE) {
                    eprintf("tail: stdout: %s\n", strerror(errno));
                } else {
                    eprintf("tail: -: %s\n", strerror(errno));
                }
                return 1;
            }
            return 0;
        }

        int file_count = argc - file_index;
        bool need_separator = false;
        int failed = 0;

        for (int i = file_index; i < argc; ++i) {
            const char *path = argv[i];
            if (!path) {
                continue;
            }

            bool show_header = should_show_header(&spec, file_count);
            if (show_header) {
                if (print_header(path, &need_separator) != 0) {
                    eprintf("tail: stdout: %s\n", strerror(errno));
                    return 1;
                }
            }

            int fd = STDIN_FILENO;
            if (strcmp(path, "-") != 0) {
                fd = open(path, O_RDONLY);
                if (fd < 0) {
                    eprintf("tail: %s: %s\n", path, strerror(errno));
                    failed = 1;
                    continue;
                }
            }

            enum io_error err = IO_OK;
            if (tail_fd(fd, &spec, &err) != 0) {
                if (err == IO_WRITE) {
                    eprintf("tail: stdout: %s\n", strerror(errno));
                    if (fd != STDIN_FILENO) {
                        (void)close(fd);
                    }
                    return 1;
                }
                eprintf("tail: %s: %s\n", path, strerror(errno));
                failed = 1;
            }
            if (fd != STDIN_FILENO) {
                (void)close(fd);
            }
        }
        return failed ? 1 : 0;
    }

    int file_count = (file_index >= argc) ? 1 : (argc - file_index);
    bool need_separator = false;
    int failed = 0;
    int last_output = -1;
    bool show_header = should_show_header(&spec, file_count);

    struct file_state *files = (struct file_state *)calloc((size_t)file_count, sizeof(*files));
    if (!files) {
        eprintf("tail: out of memory\n");
        return 1;
    }
    int follow_rc = 0;

    for (int i = 0; i < file_count; ++i) {
        const char *path = (file_index >= argc) ? "-" : argv[file_index + i];
        if (open_file_state(&files[i], path) != 0) {
            eprintf("tail: %s: %s\n", path, strerror(errno));
            files[i].fd = -1;
            failed = 1;
            continue;
        }

        if (files[i].regular) {
            if (show_header) {
                if (print_header(path, &need_separator) != 0) {
                    eprintf("tail: stdout: %s\n", strerror(errno));
                    follow_rc = 1;
                    goto follow_cleanup;
                }
                last_output = i;
            }
            enum io_error err = IO_OK;
            if (tail_fd(files[i].fd, &spec, &err) != 0) {
                if (err == IO_WRITE) {
                    eprintf("tail: stdout: %s\n", strerror(errno));
                    follow_rc = 1;
                    goto follow_cleanup;
                }
                eprintf("tail: %s: %s\n", path, strerror(errno));
                if (!files[i].is_stdin) {
                    (void)close(files[i].fd);
                }
                files[i].fd = -1;
                failed = 1;
                continue;
            }
            struct stat sb;
            if (fstat(files[i].fd, &sb) != 0) {
                eprintf("tail: %s: %s\n", path, strerror(errno));
                if (!files[i].is_stdin) {
                    (void)close(files[i].fd);
                }
                files[i].fd = -1;
                failed = 1;
                continue;
            }
            files[i].offset = sb.st_size;
        } else {
            /* Non-seekable follow: stream from current position. */
            if (spec.origin == ORIGIN_START && spec.count > 1) {
                enum io_error err = IO_OK;
                int rc = (spec.mode == COUNT_BYTES)
                    ? skip_bytes_to_start(files[i].fd, spec.count, &files[i], &err)
                    : skip_lines_to_start(files[i].fd, spec.count, &files[i], &err);
                if (rc != 0) {
                    eprintf("tail: %s: %s\n", path, strerror(errno));
                    if (!files[i].is_stdin) {
                        (void)close(files[i].fd);
                    }
                    files[i].fd = -1;
                    failed = 1;
                }
            }
        }
    }

    follow_rc = follow_files(&spec, files, file_count, &need_separator, &last_output);

follow_cleanup:
    close_file_states(files, file_count);
    free(files);
    return follow_rc ? follow_rc : (failed ? 1 : 0);
}
