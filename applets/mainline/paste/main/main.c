#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <stdbool.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

struct line_buffer {
    char *data;
    size_t len;
    size_t cap;
    bool has_newline;
};

struct line_source {
    int fd;
    bool close_fd;
    char *buf;
    size_t buf_size;
    size_t buf_len;
    size_t buf_pos;
    bool eof;
    const char *name;
};

struct paste_file {
    struct line_source *source;
    struct line_buffer line;
    bool got_line;
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

static int line_buffer_reserve(struct line_buffer *line, size_t need)
{
    if (need <= line->cap) {
        return 0;
    }
    size_t cap = line->cap == 0 ? 256 : line->cap;
    while (cap < need) {
        if (cap > SIZE_MAX / 2) {
            errno = ENOMEM;
            return -1;
        }
        cap *= 2;
    }
    char *next = (char *)realloc(line->data, cap);
    if (next == NULL) {
        return -1;
    }
    line->data = next;
    line->cap = cap;
    return 0;
}

static int line_source_init(struct line_source *source, int fd, bool close_fd, const char *name)
{
    source->fd = fd;
    source->close_fd = close_fd;
    source->buf_size = 4096;
    source->buf_len = 0;
    source->buf_pos = 0;
    source->eof = false;
    source->name = name;
    source->buf = (char *)malloc(source->buf_size);
    if (source->buf == NULL) {
        errno = ENOMEM;
        return -1;
    }
    return 0;
}

static void line_source_free(struct line_source *source)
{
    if (source->close_fd && source->fd >= 0) {
        (void)close(source->fd);
    }
    free(source->buf);
    source->buf = NULL;
}

static int read_line(struct line_source *source, struct line_buffer *line)
{
    line->len = 0;
    line->has_newline = false;

    if (source->eof) {
        return 0;
    }

    while (1) {
        if (source->buf_pos == source->buf_len) {
            ssize_t r = read_retry(source->fd, source->buf, source->buf_size);
            if (r < 0) {
                return -1;
            }
            if (r == 0) {
                source->eof = true;
                if (line->len == 0) {
                    return 0;
                }
                if (line_buffer_reserve(line, line->len + 1) != 0) {
                    return -1;
                }
                line->data[line->len] = '\0';
                return 1;
            }
            source->buf_len = (size_t)r;
            source->buf_pos = 0;
        }

        size_t start = source->buf_pos;
        size_t i = start;
        while (i < source->buf_len && source->buf[i] != '\n') {
            i++;
        }
        size_t chunk = i - start;
        if (chunk > 0) {
            if (line_buffer_reserve(line, line->len + chunk + 1) != 0) {
                return -1;
            }
            memcpy(line->data + line->len, source->buf + start, chunk);
            line->len += chunk;
        }
        if (i < source->buf_len) {
            source->buf_pos = i + 1;
            line->has_newline = true;
            if (line_buffer_reserve(line, line->len + 1) != 0) {
                return -1;
            }
            line->data[line->len] = '\0';
            return 1;
        }
        source->buf_pos = source->buf_len;
    }
}

static void usage(void)
{
    eprintf("usage: paste [-s] [-d list] [file ...]\n");
}

static int parse_delims(const char *spec, unsigned char **out, size_t *out_len)
{
    *out = NULL;
    *out_len = 0;
    if (spec == NULL || spec[0] == '\0') {
        return 0;
    }

    size_t cap = strlen(spec);
    unsigned char *buf = (unsigned char *)malloc(cap);
    if (buf == NULL) {
        errno = ENOMEM;
        return -1;
    }

    size_t len = 0;
    for (size_t i = 0; spec[i] != '\0'; ++i) {
        unsigned char c = (unsigned char)spec[i];
        if (c == '\\') {
            unsigned char next = (unsigned char)spec[i + 1];
            if (next == '\0') {
                buf[len++] = '\\';
                break;
            }
            i++;
            switch (next) {
            case 'n':
                buf[len++] = '\n';
                break;
            case 't':
                buf[len++] = '\t';
                break;
            case '0':
                buf[len++] = '\0';
                break;
            case '\\':
                buf[len++] = '\\';
                break;
            default:
                // Preserve unknown escapes as literal backslash + character.
                buf[len++] = '\\';
                buf[len++] = next;
                break;
            }
            continue;
        }
        buf[len++] = c;
    }

    *out = buf;
    *out_len = len;
    return 0;
}

static int paste_parallel(struct paste_file *files, size_t count, const unsigned char *delims, size_t delim_len)
{
    while (1) {
        bool any = false;
        for (size_t i = 0; i < count; ++i) {
            files[i].got_line = false;
            files[i].line.len = 0;
            files[i].line.has_newline = false;
            if (files[i].source->eof) {
                continue;
            }
            int rc = read_line(files[i].source, &files[i].line);
            if (rc < 0) {
                eprintf("paste: %s: %s\n", files[i].source->name, strerror(errno));
                return 1;
            }
            if (rc == 0) {
                continue;
            }
            files[i].got_line = true;
            any = true;
        }

        if (!any) {
            break;
        }

        size_t delim_index = 0;
        for (size_t i = 0; i < count; ++i) {
            if (files[i].got_line && files[i].line.len > 0) {
                if (write_all(STDOUT_FILENO, files[i].line.data, files[i].line.len) != 0) {
                    eprintf("paste: stdout: %s\n", strerror(errno));
                    return 1;
                }
            }
            if (i + 1 < count && delim_len > 0) {
                unsigned char delim = delims[delim_index++];
                if (delim_index >= delim_len) {
                    delim_index = 0;
                }
                if (write_all(STDOUT_FILENO, &delim, 1) != 0) {
                    eprintf("paste: stdout: %s\n", strerror(errno));
                    return 1;
                }
            }
        }

        char nl = '\n';
        if (write_all(STDOUT_FILENO, &nl, 1) != 0) {
            eprintf("paste: stdout: %s\n", strerror(errno));
            return 1;
        }
    }

    return 0;
}

static int paste_serial(struct paste_file *files, size_t count, const unsigned char *delims, size_t delim_len)
{
    for (size_t i = 0; i < count; ++i) {
        bool any = false;
        size_t delim_index = 0;

        while (!files[i].source->eof) {
            int rc = read_line(files[i].source, &files[i].line);
            if (rc < 0) {
                eprintf("paste: %s: %s\n", files[i].source->name, strerror(errno));
                return 1;
            }
            if (rc == 0) {
                break;
            }
            if (any && delim_len > 0) {
                unsigned char delim = delims[delim_index++];
                if (delim_index >= delim_len) {
                    delim_index = 0;
                }
                if (write_all(STDOUT_FILENO, &delim, 1) != 0) {
                    eprintf("paste: stdout: %s\n", strerror(errno));
                    return 1;
                }
            }
            if (files[i].line.len > 0) {
                if (write_all(STDOUT_FILENO, files[i].line.data, files[i].line.len) != 0) {
                    eprintf("paste: stdout: %s\n", strerror(errno));
                    return 1;
                }
            }
            any = true;
        }

        if (any) {
            char nl = '\n';
            if (write_all(STDOUT_FILENO, &nl, 1) != 0) {
                eprintf("paste: stdout: %s\n", strerror(errno));
                return 1;
            }
        }
    }

    return 0;
}

int main(int argc, char **argv)
{
    opterr = 0;
    bool serial = false;
    unsigned char default_delim = '\t';
    const unsigned char *delims = &default_delim;
    size_t delim_len = 1;
    unsigned char *delims_buf = NULL;

    int ch;
    while ((ch = getopt(argc, argv, ":sd:")) != -1) {
        switch (ch) {
        case 's':
            serial = true;
            break;
        case 'd':
            free(delims_buf);
            delims_buf = NULL;
            delim_len = 0;
            delims = NULL;
            if (parse_delims(optarg ? optarg : "", &delims_buf, &delim_len) != 0) {
                eprintf("paste: out of memory\n");
                return 1;
            }
            if (delim_len > 0) {
                delims = delims_buf;
            }
            break;
        case ':':
            eprintf("paste: option requires an argument -- %c\n", optopt);
            usage();
            return 1;
        default:
            eprintf("paste: illegal option -- %c\n", optopt);
            usage();
            return 1;
        }
    }

    size_t file_count = 0;
    if (optind < argc) {
        file_count = (size_t)(argc - optind);
    } else {
        file_count = 1;
    }
    const char *stdin_name = (optind < argc) ? "-" : "stdin";

    struct paste_file *files = (struct paste_file *)calloc(file_count, sizeof(*files));
    if (files == NULL) {
        eprintf("paste: out of memory\n");
        return 1;
    }

    struct line_source **sources = NULL;
    size_t source_count = 0;
    size_t source_cap = 0;
    struct line_source *stdin_source = NULL;
    size_t used = 0;
    int status = 0;
    bool fatal = false;

    for (size_t i = 0; i < file_count; ++i) {
        const char *path = NULL;
        if (optind < argc) {
            path = argv[optind + (int)i];
        } else {
            path = "-";
        }

        struct line_source *source = NULL;
        if (strcmp(path, "-") == 0) {
            /* Share a single buffered reader so multiple "-" entries read sequential lines. */
            if (stdin_source == NULL) {
                stdin_source = (struct line_source *)calloc(1, sizeof(*stdin_source));
                if (stdin_source == NULL) {
                    eprintf("paste: out of memory\n");
                    status = 1;
                    fatal = true;
                    break;
                }
                if (line_source_init(stdin_source, STDIN_FILENO, false, stdin_name) != 0) {
                    eprintf("paste: %s: %s\n", stdin_name, strerror(errno));
                    free(stdin_source);
                    stdin_source = NULL;
                    status = 1;
                    fatal = true;
                    break;
                }
                if (source_count == source_cap) {
                    size_t next_cap = source_cap == 0 ? 4 : source_cap * 2;
                    struct line_source **next = (struct line_source **)realloc(sources, next_cap * sizeof(*sources));
                    if (next == NULL) {
                        eprintf("paste: out of memory\n");
                        line_source_free(stdin_source);
                        free(stdin_source);
                        stdin_source = NULL;
                        status = 1;
                        fatal = true;
                        break;
                    }
                    sources = next;
                    source_cap = next_cap;
                }
                sources[source_count++] = stdin_source;
            }
            source = stdin_source;
        } else {
            int fd = open(path, O_RDONLY);
            if (fd < 0) {
                eprintf("paste: %s: %s\n", path, strerror(errno));
                status = 1;
                continue;
            }
            source = (struct line_source *)calloc(1, sizeof(*source));
            if (source == NULL) {
                eprintf("paste: out of memory\n");
                (void)close(fd);
                status = 1;
                fatal = true;
                break;
            }
            if (line_source_init(source, fd, true, path) != 0) {
                eprintf("paste: %s: %s\n", path, strerror(errno));
                (void)close(fd);
                free(source);
                status = 1;
                fatal = true;
                break;
            }
            if (source_count == source_cap) {
                size_t next_cap = source_cap == 0 ? 4 : source_cap * 2;
                struct line_source **next = (struct line_source **)realloc(sources, next_cap * sizeof(*sources));
                if (next == NULL) {
                    eprintf("paste: out of memory\n");
                    line_source_free(source);
                    free(source);
                    status = 1;
                    fatal = true;
                    break;
                }
                sources = next;
                source_cap = next_cap;
            }
            sources[source_count++] = source;
        }

        if (fatal) {
            break;
        }
        files[used].source = source;
        files[used].line.data = NULL;
        files[used].line.len = 0;
        files[used].line.cap = 0;
        files[used].line.has_newline = false;
        files[used].got_line = false;
        used++;
    }

    if (!fatal && used > 0) {
        int rc = serial ? paste_serial(files, used, delims, delim_len)
                        : paste_parallel(files, used, delims, delim_len);
        if (rc != 0) {
            status = 1;
        }
    }

    for (size_t i = 0; i < used; ++i) {
        free(files[i].line.data);
        files[i].line.data = NULL;
    }

    for (size_t i = 0; i < source_count; ++i) {
        line_source_free(sources[i]);
        free(sources[i]);
    }

    free(delims_buf);
    free(sources);
    free(files);

    return status == 0 ? 0 : 1;
}
