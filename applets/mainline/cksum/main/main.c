#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define CRC32_POLY 0x04C11DB7u

struct cksum_result {
    uint32_t crc;
    uintmax_t length;
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

static void crc_table_init(uint32_t table[256])
{
    /* Precompute the CRC-32 table used by BSD cksum (polynomial 0x04C11DB7). */
    for (unsigned int i = 0; i < 256; ++i) {
        uint32_t crc = i << 24;
        for (int bit = 0; bit < 8; ++bit) {
            if (crc & 0x80000000u) {
                crc = (crc << 1) ^ CRC32_POLY;
            } else {
                crc <<= 1;
            }
        }
        table[i] = crc;
    }
}

static uint32_t crc_update(uint32_t crc, const uint32_t table[256],
                           const unsigned char *buf, size_t len)
{
    for (size_t i = 0; i < len; ++i) {
        crc = (crc << 8) ^ table[(crc >> 24) ^ buf[i]];
    }
    return crc;
}

static int cksum_stream(int fd, const uint32_t table[256], struct cksum_result *out, int *out_err)
{
    unsigned char buf[4096];
    uint32_t crc = 0;
    uintmax_t total = 0;

    while (1) {
        ssize_t r = read_retry(fd, buf, sizeof(buf));
        if (r < 0) {
            if (out_err) {
                *out_err = errno;
            }
            return -1;
        }
        if (r == 0) {
            break;
        }
        if (total > UINTMAX_MAX - (uintmax_t)r) {
            if (out_err) {
                *out_err = EOVERFLOW;
            }
            errno = EOVERFLOW;
            return -1;
        }
        crc = crc_update(crc, table, buf, (size_t)r);
        total += (uintmax_t)r;
    }

    /* BSD/POSIX cksum appends the file length as base-256 bytes to the CRC. */
    uintmax_t len = total;
    do {
        crc = (crc << 8) ^ table[(crc >> 24) ^ (unsigned char)(len & 0xffu)];
        len >>= 8;
    } while (len != 0);

    out->crc = ~crc;
    out->length = total;
    return 0;
}

static int write_result(const struct cksum_result *result, const char *name, bool print_name)
{
    char buf[64];
    int n = snprintf(buf, sizeof(buf), "%" PRIuMAX " %" PRIuMAX,
                     (uintmax_t)result->crc, result->length);
    if (n < 0 || (size_t)n >= sizeof(buf)) {
        errno = EOVERFLOW;
        return -1;
    }

    if (write_all(STDOUT_FILENO, buf, (size_t)n) != 0) {
        return -1;
    }
    if (print_name) {
        size_t name_len = strlen(name);
        if (write_all(STDOUT_FILENO, " ", 1) != 0 ||
            write_all(STDOUT_FILENO, name, name_len) != 0) {
            return -1;
        }
    }
    if (write_all(STDOUT_FILENO, "\n", 1) != 0) {
        return -1;
    }
    return 0;
}

int main(int argc, char **argv)
{
    uint32_t table[256];
    crc_table_init(table);

    if (argc == 1) {
        struct cksum_result result;
        int err = 0;
        if (cksum_stream(STDIN_FILENO, table, &result, &err) != 0) {
            eprintf("cksum: stdin: %s\n", strerror(err));
            return 1;
        }
        if (write_result(&result, NULL, false) != 0) {
            eprintf("cksum: stdout: %s\n", strerror(errno));
            return 1;
        }
        return 0;
    }

    int failed = 0;
    for (int i = 1; i < argc; ++i) {
        const char *name = argv[i];
        bool is_stdin = false;
        int fd = -1;

        if (name && strcmp(name, "-") == 0) {
            name = "-";
            is_stdin = true;
            fd = STDIN_FILENO;
        } else {
            fd = open(name, O_RDONLY);
        }

        if (fd < 0) {
            eprintf("cksum: %s: %s\n", name ? name : "(null)", strerror(errno));
            failed = 1;
            continue;
        }

        struct cksum_result result;
        int err = 0;
        if (cksum_stream(fd, table, &result, &err) != 0) {
            eprintf("cksum: %s: %s\n", name ? name : "(null)", strerror(err));
            failed = 1;
        } else if (write_result(&result, name, true) != 0) {
            eprintf("cksum: stdout: %s\n", strerror(errno));
            if (!is_stdin) {
                (void)close(fd);
            }
            return 1;
        }

        if (!is_stdin) {
            (void)close(fd);
        }
    }

    return failed;
}
