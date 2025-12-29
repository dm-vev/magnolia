#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

struct wc_flags {
    bool lines;
    bool words;
    bool bytes;
    bool chars;
    bool max_line;
};

struct wc_counts {
    uintmax_t lines;
    uintmax_t words;
    uintmax_t bytes;
    uintmax_t chars;
    uintmax_t max_line;
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

static void usage(void)
{
    eprintf("usage: wc [-clmwL] [file ...]\n");
}

static void counts_reset(struct wc_counts *counts)
{
    counts->lines = 0;
    counts->words = 0;
    counts->bytes = 0;
    counts->chars = 0;
    counts->max_line = 0;
}

/* TODO: If Magnolia gains multibyte locale support, honor BSD wc -m/-L with wide characters. */
static int count_stream(int fd, struct wc_counts *counts, bool count_chars, bool count_max, bool *read_failed)
{
    unsigned char buf[4096];
    bool in_word = false;
    uintmax_t line_len = 0;

    while (1) {
        ssize_t r = read_retry(fd, buf, sizeof(buf));
        if (r < 0) {
            if (count_max && line_len > counts->max_line) {
                counts->max_line = line_len;
            }
            if (read_failed) {
                *read_failed = true;
            }
            return -1;
        }
        if (r == 0) {
            break;
        }
        for (ssize_t i = 0; i < r; ++i) {
            unsigned char c = buf[i];
            counts->bytes++;
            if (count_chars) {
                counts->chars++;
            }
            if (c == '\n') {
                counts->lines++;
                if (count_max && line_len > counts->max_line) {
                    counts->max_line = line_len;
                }
                line_len = 0;
            } else if (count_max) {
                line_len++;
            }
            if (isspace(c)) {
                in_word = false;
            } else if (!in_word) {
                counts->words++;
                in_word = true;
            }
        }
    }

    if (count_max && line_len > counts->max_line) {
        counts->max_line = line_len;
    }
    return 0;
}

static int write_field(uintmax_t value, bool first)
{
    char buf[32];
    const char *fmt = first ? "%7" PRIuMAX : " %7" PRIuMAX;
    int n = snprintf(buf, sizeof(buf), fmt, value);
    if (n < 0) {
        errno = EIO;
        return -1;
    }
    if ((size_t)n >= sizeof(buf)) {
        errno = EOVERFLOW;
        return -1;
    }
    return write_all(STDOUT_FILENO, buf, (size_t)n);
}

static int write_counts(const struct wc_counts *counts, const struct wc_flags *flags,
                        const char *name, bool print_name)
{
    bool first = true;

    if (flags->lines) {
        if (write_field(counts->lines, first) != 0) {
            return -1;
        }
        first = false;
    }
    if (flags->words) {
        if (write_field(counts->words, first) != 0) {
            return -1;
        }
        first = false;
    }
    if (flags->bytes) {
        if (write_field(counts->bytes, first) != 0) {
            return -1;
        }
        first = false;
    }
    if (flags->chars) {
        if (write_field(counts->chars, first) != 0) {
            return -1;
        }
        first = false;
    }
    if (flags->max_line) {
        if (write_field(counts->max_line, first) != 0) {
            return -1;
        }
        first = false;
    }

    if (print_name) {
        if (write_all(STDOUT_FILENO, " ", 1) != 0) {
            return -1;
        }
        if (name) {
            size_t name_len = strlen(name);
            if (write_all(STDOUT_FILENO, name, name_len) != 0) {
                return -1;
            }
        }
    }

    return write_all(STDOUT_FILENO, "\n", 1);
}

int main(int argc, char **argv)
{
    opterr = 0;

    struct wc_flags flags = {
        .lines = false,
        .words = false,
        .bytes = false,
        .chars = false,
        .max_line = false,
    };

    int ch;
    while ((ch = getopt(argc, argv, "clmwL")) != -1) {
        switch (ch) {
        case 'c':
            flags.bytes = true;
            break;
        case 'l':
            flags.lines = true;
            break;
        case 'm':
            flags.chars = true;
            break;
        case 'w':
            flags.words = true;
            break;
        case 'L':
            flags.max_line = true;
            break;
        default:
            eprintf("wc: illegal option -- %c\n", optopt);
            usage();
            return 1;
        }
    }

    if (!flags.lines && !flags.words && !flags.bytes && !flags.chars && !flags.max_line) {
        flags.lines = true;
        flags.words = true;
        flags.bytes = true;
    }

    const int operand_count = argc - optind;
    const bool print_name = (operand_count > 0);
    const bool print_total = (operand_count > 1);

    int failed = 0;
    bool any_counted = false;
    struct wc_counts total;
    counts_reset(&total);

    if (operand_count == 0) {
        struct wc_counts counts;
        counts_reset(&counts);
        bool read_failed = false;
        (void)count_stream(STDIN_FILENO, &counts, flags.chars, flags.max_line, &read_failed);
        if (read_failed) {
            eprintf("wc: stdin: %s\n", strerror(errno));
            failed = 1;
        }
        if (write_counts(&counts, &flags, NULL, false) != 0) {
            eprintf("wc: stdout: %s\n", strerror(errno));
            return 1;
        }
        return failed;
    }

    for (int i = optind; i < argc; ++i) {
        const char *name = argv[i];
        bool is_stdin = false;
        int fd = -1;

        if (!name || strcmp(name, "-") == 0) {
            name = "-";
            is_stdin = true;
            fd = STDIN_FILENO;
        } else {
            fd = open(name, O_RDONLY);
        }

        if (fd < 0) {
            eprintf("wc: %s: %s\n", name ? name : "(null)", strerror(errno));
            failed = 1;
            continue;
        }

        struct wc_counts counts;
        counts_reset(&counts);
        bool read_failed = false;
        int read_errno = 0;
        (void)count_stream(fd, &counts, flags.chars, flags.max_line, &read_failed);
        if (read_failed) {
            read_errno = errno;
        }
        if (!is_stdin) {
            (void)close(fd);
        }

        if (read_failed) {
            eprintf("wc: %s: %s\n", name, strerror(read_errno));
            failed = 1;
        }

        if (write_counts(&counts, &flags, name, print_name) != 0) {
            eprintf("wc: stdout: %s\n", strerror(errno));
            return 1;
        }

        any_counted = true;
        total.lines += counts.lines;
        total.words += counts.words;
        total.bytes += counts.bytes;
        total.chars += counts.chars;
        if (counts.max_line > total.max_line) {
            total.max_line = counts.max_line;
        }
    }

    if (print_total && any_counted) {
        if (write_counts(&total, &flags, "total", true) != 0) {
            eprintf("wc: stdout: %s\n", strerror(errno));
            return 1;
        }
    }

    return failed;
}
