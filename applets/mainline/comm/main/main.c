#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* BSD reference: FreeBSD comm(1). */

struct line_buffer {
    char *data;
    size_t len;
    bool has_newline;
};

struct line_reader {
    int fd;
    char *buf;
    size_t buf_size;
    size_t buf_len;
    size_t buf_pos;
    struct line_buffer line;
    size_t line_cap;
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

static bool streq(const char *a, const char *b)
{
    return a && b && strcmp(a, b) == 0;
}

static int line_reader_init(struct line_reader *reader, int fd)
{
    reader->fd = fd;
    reader->buf_size = 4096;
    reader->buf_len = 0;
    reader->buf_pos = 0;
    reader->buf = (char *)malloc(reader->buf_size);
    if (reader->buf == NULL) {
        errno = ENOMEM;
        return -1;
    }
    reader->line_cap = 256;
    reader->line.data = (char *)malloc(reader->line_cap);
    if (reader->line.data == NULL) {
        errno = ENOMEM;
        free(reader->buf);
        reader->buf = NULL;
        return -1;
    }
    reader->line.len = 0;
    reader->line.has_newline = false;
    return 0;
}

static void line_reader_free(struct line_reader *reader)
{
    free(reader->buf);
    free(reader->line.data);
    reader->buf = NULL;
    reader->line.data = NULL;
}

static int line_reader_reserve(struct line_reader *reader, size_t need)
{
    if (need <= reader->line_cap) {
        return 0;
    }
    size_t cap = reader->line_cap;
    while (cap < need) {
        if (cap > SIZE_MAX / 2) {
            errno = ENOMEM;
            return -1;
        }
        cap *= 2;
    }
    char *next = (char *)realloc(reader->line.data, cap);
    if (next == NULL) {
        errno = ENOMEM;
        return -1;
    }
    reader->line.data = next;
    reader->line_cap = cap;
    return 0;
}

static int read_line(struct line_reader *reader)
{
    reader->line.len = 0;
    reader->line.has_newline = false;

    /* Preserve newline presence so output matches input lines. */
    while (1) {
        if (reader->buf_pos == reader->buf_len) {
            ssize_t r = read_retry(reader->fd, reader->buf, reader->buf_size);
            if (r < 0) {
                return -1;
            }
            if (r == 0) {
                if (reader->line.len == 0) {
                    return 0;
                }
                // Guard size_t wrap on extremely long lines before reserving space for NUL.
                if (reader->line.len > SIZE_MAX - 1) {
                    errno = EOVERFLOW;
                    return -1;
                }
                if (line_reader_reserve(reader, reader->line.len + 1) != 0) {
                    return -1;
                }
                reader->line.data[reader->line.len] = '\0';
                return 1;
            }
            reader->buf_len = (size_t)r;
            reader->buf_pos = 0;
        }

        size_t start = reader->buf_pos;
        size_t i = start;
        while (i < reader->buf_len && reader->buf[i] != '\n') {
            i++;
        }
        size_t chunk = i - start;
        if (chunk > 0) {
            // Guard size_t wrap when extending the line buffer.
            if (reader->line.len > SIZE_MAX - chunk - 1) {
                errno = EOVERFLOW;
                return -1;
            }
            if (line_reader_reserve(reader, reader->line.len + chunk + 1) != 0) {
                return -1;
            }
            memcpy(reader->line.data + reader->line.len, reader->buf + start, chunk);
            reader->line.len += chunk;
        }
        if (i < reader->buf_len) {
            reader->buf_pos = i + 1;
            reader->line.has_newline = true;
            // Guard size_t wrap on extremely long lines before reserving space for NUL.
            if (reader->line.len > SIZE_MAX - 1) {
                errno = EOVERFLOW;
                return -1;
            }
            if (line_reader_reserve(reader, reader->line.len + 1) != 0) {
                return -1;
            }
            reader->line.data[reader->line.len] = '\0';
            return 1;
        }
        reader->buf_pos = reader->buf_len;
    }
}

static int line_compare(const struct line_buffer *a, const struct line_buffer *b)
{
    size_t min = a->len < b->len ? a->len : b->len;
    int cmp = memcmp(a->data, b->data, min);
    if (cmp != 0) {
        return cmp;
    }
    if (a->len < b->len) {
        return -1;
    }
    if (a->len > b->len) {
        return 1;
    }
    return 0;
}

static int write_tabs(unsigned count)
{
    if (count == 0) {
        return 0;
    }
    static const char tabs[] = "\t\t\t\t\t\t\t\t";
    size_t remaining = count;
    while (remaining > 0) {
        size_t chunk = remaining > (sizeof(tabs) - 1) ? (sizeof(tabs) - 1) : remaining;
        if (write_all(STDOUT_FILENO, tabs, chunk) != 0) {
            return -1;
        }
        remaining -= chunk;
    }
    return 0;
}

static int output_line(unsigned tabs, const struct line_buffer *line)
{
    if (write_tabs(tabs) != 0) {
        return -1;
    }
    if (line->len > 0) {
        if (write_all(STDOUT_FILENO, line->data, line->len) != 0) {
            return -1;
        }
    }
    if (line->has_newline) {
        static const char nl = '\n';
        if (write_all(STDOUT_FILENO, &nl, 1) != 0) {
            return -1;
        }
    }
    return 0;
}

int main(int argc, char **argv)
{
    bool show1 = true;
    bool show2 = true;
    bool show3 = true;

    int opt;
    while ((opt = getopt(argc, argv, "123")) != -1) {
        switch (opt) {
        case '1':
            show1 = false;
            break;
        case '2':
            show2 = false;
            break;
        case '3':
            show3 = false;
            break;
        default:
            eprintf("usage: comm [-123] file1 file2\n");
            return 2;
        }
    }

    if (argc - optind != 2) {
        eprintf("usage: comm [-123] file1 file2\n");
        return 2;
    }

    const char *name1 = argv[optind];
    const char *name2 = argv[optind + 1];

    unsigned tabs1 = 0;
    unsigned tabs2 = show1 ? 1 : 0;
    unsigned tabs3 = (show1 ? 1 : 0) + (show2 ? 1 : 0);

    if (streq(name1, "-") && streq(name2, "-")) {
        struct line_reader reader;
        if (line_reader_init(&reader, STDIN_FILENO) != 0) {
            eprintf("comm: -: %s\n", strerror(errno));
            return 2;
        }
        int rc = 0;
        bool output_failed = false;
        while ((rc = read_line(&reader)) > 0) {
            if (show3) {
                if (output_line(tabs3, &reader.line) != 0) {
                    eprintf("comm: stdout: %s\n", strerror(errno));
                    output_failed = true;
                    break;
                }
            }
        }
        if (output_failed) {
            line_reader_free(&reader);
            return 2;
        }
        if (rc < 0 && !output_failed) {
            eprintf("comm: -: %s\n", strerror(errno));
            line_reader_free(&reader);
            return 2;
        }
        line_reader_free(&reader);
        return 0;
    }

    int fd1 = -1;
    int fd2 = -1;
    bool close1 = false;
    bool close2 = false;

    if (streq(name1, "-")) {
        fd1 = STDIN_FILENO;
    } else {
        fd1 = open(name1, O_RDONLY);
        if (fd1 < 0) {
            eprintf("comm: %s: %s\n", name1, strerror(errno));
            return 2;
        }
        close1 = true;
    }

    if (streq(name2, "-")) {
        fd2 = STDIN_FILENO;
    } else {
        fd2 = open(name2, O_RDONLY);
        if (fd2 < 0) {
            eprintf("comm: %s: %s\n", name2, strerror(errno));
            if (close1) {
                (void)close(fd1);
            }
            return 2;
        }
        close2 = true;
    }

    struct line_reader reader1;
    struct line_reader reader2;
    if (line_reader_init(&reader1, fd1) != 0) {
        eprintf("comm: %s: %s\n", name1, strerror(errno));
        if (close1) {
            (void)close(fd1);
        }
        if (close2) {
            (void)close(fd2);
        }
        return 2;
    }
    if (line_reader_init(&reader2, fd2) != 0) {
        eprintf("comm: %s: %s\n", name2, strerror(errno));
        line_reader_free(&reader1);
        if (close1) {
            (void)close(fd1);
        }
        if (close2) {
            (void)close(fd2);
        }
        return 2;
    }

    int rc1 = read_line(&reader1);
    int rc2 = read_line(&reader2);
    int exit_code = 0;

    while (rc1 > 0 && rc2 > 0) {
        int cmp = line_compare(&reader1.line, &reader2.line);
        if (cmp < 0) {
            if (show1) {
                if (output_line(tabs1, &reader1.line) != 0) {
                    eprintf("comm: stdout: %s\n", strerror(errno));
                    exit_code = 2;
                    break;
                }
            }
            rc1 = read_line(&reader1);
        } else if (cmp > 0) {
            if (show2) {
                if (output_line(tabs2, &reader2.line) != 0) {
                    eprintf("comm: stdout: %s\n", strerror(errno));
                    exit_code = 2;
                    break;
                }
            }
            rc2 = read_line(&reader2);
        } else {
            if (show3) {
                if (output_line(tabs3, &reader1.line) != 0) {
                    eprintf("comm: stdout: %s\n", strerror(errno));
                    exit_code = 2;
                    break;
                }
            }
            rc1 = read_line(&reader1);
            rc2 = read_line(&reader2);
        }
    }

    if (exit_code == 0) {
        if (rc1 < 0) {
            eprintf("comm: %s: %s\n", name1, strerror(errno));
            exit_code = 2;
        } else if (rc2 < 0) {
            eprintf("comm: %s: %s\n", name2, strerror(errno));
            exit_code = 2;
        }
    }

    while (exit_code == 0 && rc1 > 0) {
        if (show1) {
            if (output_line(tabs1, &reader1.line) != 0) {
                eprintf("comm: stdout: %s\n", strerror(errno));
                exit_code = 2;
                break;
            }
        }
        rc1 = read_line(&reader1);
    }

    if (exit_code == 0 && rc1 < 0) {
        eprintf("comm: %s: %s\n", name1, strerror(errno));
        exit_code = 2;
    }

    while (exit_code == 0 && rc2 > 0) {
        if (show2) {
            if (output_line(tabs2, &reader2.line) != 0) {
                eprintf("comm: stdout: %s\n", strerror(errno));
                exit_code = 2;
                break;
            }
        }
        rc2 = read_line(&reader2);
    }

    if (exit_code == 0 && rc2 < 0) {
        eprintf("comm: %s: %s\n", name2, strerror(errno));
        exit_code = 2;
    }

    line_reader_free(&reader1);
    line_reader_free(&reader2);

    if (close1) {
        (void)close(fd1);
    }
    if (close2) {
        (void)close(fd2);
    }

    return exit_code;
}
