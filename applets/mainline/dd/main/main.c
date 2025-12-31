#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

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

static void usage(void)
{
    eprintf("usage: dd [if=file] [of=file] [ibs=n] [obs=n] [bs=n] [count=n] [skip=n] [seek=n] [conv=...] [status=none]\n");
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

static int skip_input(int fd, uint64_t blocks, size_t ibs)
{
    if (blocks == 0) {
        return 0;
    }
    if (ibs > 0 && blocks > (uint64_t)LLONG_MAX / ibs) {
        errno = EOVERFLOW;
        return -1;
    }
    off_t offset = (off_t)(blocks * ibs);
    if ((uint64_t)offset != blocks * ibs) {
        errno = EOVERFLOW;
        return -1;
    }
    if (lseek(fd, offset, SEEK_CUR) >= 0) {
        return 0;
    }
    if (errno != ESPIPE && errno != EINVAL) {
        return -1;
    }
    unsigned char *buf = (unsigned char *)malloc(ibs);
    if (!buf) {
        return -1;
    }
    uint64_t left = blocks;
    while (left > 0) {
        ssize_t r = read_retry(fd, buf, ibs);
        if (r <= 0) {
            free(buf);
            return r == 0 ? 0 : -1;
        }
        left--;
    }
    free(buf);
    return 0;
}

static int seek_output(int fd, uint64_t blocks, size_t obs)
{
    if (blocks == 0) {
        return 0;
    }
    if (obs > 0 && blocks > (uint64_t)LLONG_MAX / obs) {
        errno = EOVERFLOW;
        return -1;
    }
    off_t offset = (off_t)(blocks * obs);
    if ((uint64_t)offset != blocks * obs) {
        errno = EOVERFLOW;
        return -1;
    }
    if (lseek(fd, offset, SEEK_CUR) >= 0) {
        return 0;
    }
    if (errno != ESPIPE && errno != EINVAL) {
        return -1;
    }
    unsigned char *zeros = (unsigned char *)calloc(1, obs);
    if (!zeros) {
        return -1;
    }
    uint64_t left = blocks;
    while (left > 0) {
        if (write_all(fd, zeros, obs) != 0) {
            free(zeros);
            return -1;
        }
        left--;
    }
    free(zeros);
    return 0;
}

int main(int argc, char **argv)
{
    const char *ifile = NULL;
    const char *ofile = NULL;
    uint64_t ibs = 512;
    uint64_t obs = 512;
    uint64_t bs = 0;
    uint64_t count = 0;
    uint64_t skip = 0;
    uint64_t seek = 0;
    bool use_count = false;
    bool noerror = false;
    bool sync = false;
    bool notrunc = false;
    bool status_none = false;

    if (argc <= 1) {
        usage();
        return 1;
    }

    for (int i = 1; i < argc; ++i) {
        const char *arg = argv[i];
        const char *eq = strchr(arg, '=');
        if (!eq) {
            eprintf("dd: invalid argument '%s'\n", arg);
            return 1;
        }
        size_t key_len = (size_t)(eq - arg);
        const char *val = eq + 1;
        if (key_len == 2 && strncmp(arg, "if", 2) == 0) {
            ifile = val;
        } else if (key_len == 2 && strncmp(arg, "of", 2) == 0) {
            ofile = val;
        } else if (key_len == 3 && strncmp(arg, "ibs", 3) == 0) {
            if (parse_size(val, &ibs) != 0) {
                eprintf("dd: invalid ibs '%s'\n", val);
                return 1;
            }
        } else if (key_len == 3 && strncmp(arg, "obs", 3) == 0) {
            if (parse_size(val, &obs) != 0) {
                eprintf("dd: invalid obs '%s'\n", val);
                return 1;
            }
        } else if (key_len == 2 && strncmp(arg, "bs", 2) == 0) {
            if (parse_size(val, &bs) != 0) {
                eprintf("dd: invalid bs '%s'\n", val);
                return 1;
            }
        } else if (key_len == 5 && strncmp(arg, "count", 5) == 0) {
            if (parse_size(val, &count) != 0) {
                eprintf("dd: invalid count '%s'\n", val);
                return 1;
            }
            use_count = true;
        } else if (key_len == 4 && strncmp(arg, "skip", 4) == 0) {
            if (parse_size(val, &skip) != 0) {
                eprintf("dd: invalid skip '%s'\n", val);
                return 1;
            }
        } else if (key_len == 4 && strncmp(arg, "seek", 4) == 0) {
            if (parse_size(val, &seek) != 0) {
                eprintf("dd: invalid seek '%s'\n", val);
                return 1;
            }
        } else if (key_len == 4 && strncmp(arg, "conv", 4) == 0) {
            const char *p = val;
            while (*p) {
                const char *comma = strchr(p, ',');
                size_t len = comma ? (size_t)(comma - p) : strlen(p);
                if (len == 7 && strncmp(p, "noerror", len) == 0) {
                    noerror = true;
                } else if (len == 4 && strncmp(p, "sync", len) == 0) {
                    sync = true;
                } else if (len == 7 && strncmp(p, "notrunc", len) == 0) {
                    notrunc = true;
                } else if (len > 0) {
                    eprintf("dd: unsupported conv '%.*s'\n", (int)len, p);
                    return 1;
                }
                if (!comma) {
                    break;
                }
                p = comma + 1;
            }
        } else if (key_len == 6 && strncmp(arg, "status", 6) == 0) {
            if (strcmp(val, "none") == 0) {
                status_none = true;
            } else {
                eprintf("dd: unsupported status '%s'\n", val);
                return 1;
            }
        } else {
            eprintf("dd: invalid argument '%s'\n", arg);
            return 1;
        }
    }

    if (bs > 0) {
        ibs = bs;
        obs = bs;
    }
    if (ibs == 0 || obs == 0) {
        eprintf("dd: block size cannot be zero\n");
        return 1;
    }

    int in_fd = STDIN_FILENO;
    int out_fd = STDOUT_FILENO;
    if (ifile) {
        in_fd = open(ifile, O_RDONLY);
        if (in_fd < 0) {
            eprintf("dd: %s: %s\n", ifile, strerror(errno));
            return 1;
        }
    }

    if (ofile) {
        int flags = O_WRONLY | O_CREAT;
        if (!notrunc) {
            flags |= O_TRUNC;
        }
        out_fd = open(ofile, flags, 0666);
        if (out_fd < 0) {
            eprintf("dd: %s: %s\n", ofile, strerror(errno));
            if (ifile) {
                close(in_fd);
            }
            return 1;
        }
    }

    if (skip_input(in_fd, skip, (size_t)ibs) != 0) {
        eprintf("dd: skip failed: %s\n", strerror(errno));
        if (ifile) {
            close(in_fd);
        }
        if (ofile) {
            close(out_fd);
        }
        return 1;
    }

    if (seek_output(out_fd, seek, (size_t)obs) != 0) {
        eprintf("dd: seek failed: %s\n", strerror(errno));
        if (ifile) {
            close(in_fd);
        }
        if (ofile) {
            close(out_fd);
        }
        return 1;
    }

    unsigned char *ibuf = (unsigned char *)malloc((size_t)ibs);
    unsigned char *obuf = (unsigned char *)malloc((size_t)obs);
    if (!ibuf || !obuf) {
        eprintf("dd: out of memory\n");
        if (ifile) {
            close(in_fd);
        }
        if (ofile) {
            close(out_fd);
        }
        free(ibuf);
        free(obuf);
        return 1;
    }

    uint64_t in_full = 0;
    uint64_t in_part = 0;
    uint64_t out_full = 0;
    uint64_t out_part = 0;
    uint64_t blocks = 0;
    size_t obuf_len = 0;
    int exit_status = 0;

    while (!use_count || blocks < count) {
        bool read_error_block = false;
        bool have_block = false;
        size_t n = 0;
        ssize_t r = read_retry(in_fd, ibuf, (size_t)ibs);
        if (r < 0) {
            eprintf("dd: read error: %s\n", strerror(errno));
            exit_status = 1;
            if (noerror) {
                if (sync) {
                    memset(ibuf, 0, (size_t)ibs);
                    read_error_block = true;
                    n = (size_t)ibs;
                    have_block = true;
                } else {
                    /* Skip a failed input block to avoid retrying the same offset forever. */
                    if (ibs > (size_t)LLONG_MAX) {
                        break;
                    }
                    if (lseek(in_fd, (off_t)ibs, SEEK_CUR) >= 0) {
                        blocks++;
                        continue;
                    }
                    break;
                }
            } else {
                break;
            }
        } else if (r == 0) {
            break;
        }
        if (!have_block) {
            n = (size_t)r;
            have_block = true;
        }
        if (read_error_block) {
            in_part++;
        } else {
            if (n == (size_t)ibs) {
                in_full++;
            } else {
                in_part++;
            }
        }
        if (sync && n < (size_t)ibs) {
            memset(ibuf + n, 0, (size_t)ibs - n);
            n = (size_t)ibs;
        }
        if (obs == ibs) {
            if (write_all(out_fd, ibuf, n) != 0) {
                eprintf("dd: write error: %s\n", strerror(errno));
                exit_status = 1;
                break;
            }
            if (n == (size_t)obs) {
                out_full++;
            } else {
                out_part++;
            }
        } else {
            size_t off = 0;
            while (off < n) {
                size_t chunk = n - off;
                size_t space = (size_t)obs - obuf_len;
                if (chunk > space) {
                    chunk = space;
                }
                memcpy(obuf + obuf_len, ibuf + off, chunk);
                obuf_len += chunk;
                off += chunk;
                if (obuf_len == (size_t)obs) {
                    if (write_all(out_fd, obuf, (size_t)obs) != 0) {
                        eprintf("dd: write error: %s\n", strerror(errno));
                        exit_status = 1;
                        off = n;
                        obuf_len = 0;
                        goto done;
                    }
                    out_full++;
                    obuf_len = 0;
                }
            }
        }
        blocks++;
    }

    if (obuf_len > 0) {
        if (write_all(out_fd, obuf, obuf_len) != 0) {
            eprintf("dd: write error: %s\n", strerror(errno));
            exit_status = 1;
        } else {
            out_part++;
        }
    }

done:
    if (!status_none) {
        eprintf("%llu+%llu records in\n", (unsigned long long)in_full, (unsigned long long)in_part);
        eprintf("%llu+%llu records out\n", (unsigned long long)out_full, (unsigned long long)out_part);
    }

    if (ifile) {
        close(in_fd);
    }
    if (ofile) {
        close(out_fd);
    }
    free(ibuf);
    free(obuf);
    return exit_status;
}
