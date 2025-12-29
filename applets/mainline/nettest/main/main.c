#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#define NETTEST_DEFAULT_MSG "magnolia-nettest"
#define NETTEST_DEFAULT_TIMEOUT_MS 3000
#define NETTEST_MAX_MSG 256
#define NETTEST_STRESS_SOCKETS 8
#define NETTEST_CLOSE_DELAY_MS 100

typedef enum {
    NETTEST_OK = 0,
    NETTEST_ERR_USAGE = 1,
    NETTEST_ERR_UDP_SOCKET = 2,
    NETTEST_ERR_UDP_ADDR = 3,
    NETTEST_ERR_UDP_SEND = 4,
    NETTEST_ERR_UDP_RECV = 5,
    NETTEST_ERR_UDP_MISMATCH = 6,
    NETTEST_ERR_TCP_SOCKET = 7,
    NETTEST_ERR_TCP_ADDR = 8,
    NETTEST_ERR_TCP_CONNECT = 9,
    NETTEST_ERR_TCP_SEND = 10,
    NETTEST_ERR_TCP_RECV = 11,
    NETTEST_ERR_TCP_MISMATCH = 12,
    NETTEST_ERR_TIMEOUT_TEST = 13,
    NETTEST_ERR_CLOSED_FD = 14,
    NETTEST_ERR_CONNECT_NEG = 15,
    NETTEST_ERR_PARALLEL = 16,
    NETTEST_ERR_STRESS = 17,
    NETTEST_ERR_THREAD = 18,
    NETTEST_ERR_CLOSE_IO = 19,
    NETTEST_ERR_LEAK = 20,
} nettest_error_t;

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

static void usage(const char *argv0)
{
    eprintf("usage: %s <udp_host> <udp_port> <tcp_host> <tcp_port> [msg] [timeout_ms] [--all|--stress|--parallel|--timeout|--negative|--leak]\n",
            argv0);
}

static bool parse_port(const char *s, int *out)
{
    if (s == NULL || out == NULL) {
        return false;
    }
    char *end = NULL;
    long v = strtol(s, &end, 10);
    if (end == s || *end != '\0' || v < 1 || v > 65535) {
        return false;
    }
    *out = (int)v;
    return true;
}

static bool parse_timeout(const char *s, int *out)
{
    if (s == NULL || out == NULL) {
        return false;
    }
    char *end = NULL;
    long v = strtol(s, &end, 10);
    if (end == s || *end != '\0' || v < 0 || v > 60000) {
        return false;
    }
    *out = (int)v;
    return true;
}

typedef struct {
    bool stress;
    bool parallel;
    bool timeout;
    bool negative;
    bool leak;
} nettest_flags_t;

static bool parse_args(int argc,
                       char **argv,
                       int start,
                       const char **msg,
                       int *timeout_ms,
                       nettest_flags_t *flags)
{
    if (msg == NULL || timeout_ms == NULL || flags == NULL) {
        return false;
    }

    bool have_msg = false;
    bool have_timeout = false;

    for (int i = start; i < argc; ++i) {
        const char *arg = argv[i];
        if (arg == NULL) {
            continue;
        }
        if (arg[0] == '-' && arg[1] == '-') {
            if (strcmp(arg, "--stress") == 0) {
                flags->stress = true;
            } else if (strcmp(arg, "--parallel") == 0) {
                flags->parallel = true;
            } else if (strcmp(arg, "--timeout") == 0) {
                flags->timeout = true;
            } else if (strcmp(arg, "--negative") == 0) {
                flags->negative = true;
            } else if (strcmp(arg, "--leak") == 0) {
                flags->leak = true;
            } else if (strcmp(arg, "--all") == 0) {
                flags->stress = true;
                flags->parallel = true;
                flags->timeout = true;
                flags->negative = true;
            } else {
                return false;
            }
            continue;
        }

        if (!have_msg) {
            *msg = arg;
            have_msg = true;
            continue;
        }
        if (!have_timeout) {
            if (!parse_timeout(arg, timeout_ms)) {
                return false;
            }
            have_timeout = true;
            continue;
        }
        return false;
    }

    return true;
}

static uint64_t nettest_now_ms(void)
{
    struct timeval tv;
    if (gettimeofday(&tv, NULL) != 0) {
        return 0;
    }
    return (uint64_t)tv.tv_sec * 1000ULL + (uint64_t)tv.tv_usec / 1000ULL;
}

static int set_socket_timeout(int fd, int timeout_ms)
{
    struct timeval tv = {
        .tv_sec = timeout_ms / 1000,
        .tv_usec = (timeout_ms % 1000) * 1000,
    };
    if (setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0) {
        return -1;
    }
    if (setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv)) < 0) {
        return -1;
    }
    return 0;
}

static int udp_echo_test(const char *host, int port, const char *msg, int timeout_ms)
{
    int fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (fd < 0) {
        eprintf("udp: socket failed errno=%d\n", errno);
        return NETTEST_ERR_UDP_SOCKET;
    }

    if (set_socket_timeout(fd, timeout_ms) < 0) {
        eprintf("udp: setsockopt failed errno=%d\n", errno);
    }

    struct sockaddr_in dst = {0};
    dst.sin_family = AF_INET;
    dst.sin_port = htons((uint16_t)port);
    if (inet_pton(AF_INET, host, &dst.sin_addr) != 1) {
        eprintf("udp: invalid address '%s'\n", host);
        close(fd);
        return NETTEST_ERR_UDP_ADDR;
    }

    size_t msg_len = strlen(msg);
    ssize_t sent = sendto(fd, msg, msg_len, 0, (struct sockaddr *)&dst, sizeof(dst));
    if (sent < 0 || (size_t)sent != msg_len) {
        eprintf("udp: send failed errno=%d\n", errno);
        close(fd);
        return NETTEST_ERR_UDP_SEND;
    }

    char buf[NETTEST_MAX_MSG];
    struct sockaddr_in src = {0};
    socklen_t src_len = sizeof(src);
    ssize_t got = recvfrom(fd, buf, sizeof(buf), 0, (struct sockaddr *)&src, &src_len);
    if (got < 0) {
        eprintf("udp: recv failed errno=%d\n", errno);
        close(fd);
        return NETTEST_ERR_UDP_RECV;
    }
    if ((size_t)got != msg_len || memcmp(buf, msg, msg_len) != 0) {
        eprintf("udp: echo mismatch (len=%ld)\n", (long)got);
        close(fd);
        return NETTEST_ERR_UDP_MISMATCH;
    }

    close(fd);
    return NETTEST_OK;
}

static int tcp_echo_test(const char *host, int port, const char *msg, int timeout_ms)
{
    int fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (fd < 0) {
        eprintf("tcp: socket failed errno=%d\n", errno);
        return NETTEST_ERR_TCP_SOCKET;
    }

    if (set_socket_timeout(fd, timeout_ms) < 0) {
        eprintf("tcp: setsockopt failed errno=%d\n", errno);
    }

    struct sockaddr_in dst = {0};
    dst.sin_family = AF_INET;
    dst.sin_port = htons((uint16_t)port);
    if (inet_pton(AF_INET, host, &dst.sin_addr) != 1) {
        eprintf("tcp: invalid address '%s'\n", host);
        close(fd);
        return NETTEST_ERR_TCP_ADDR;
    }

    if (connect(fd, (struct sockaddr *)&dst, sizeof(dst)) < 0) {
        eprintf("tcp: connect failed errno=%d\n", errno);
        close(fd);
        return NETTEST_ERR_TCP_CONNECT;
    }

    size_t msg_len = strlen(msg);
    ssize_t sent = send(fd, msg, msg_len, 0);
    if (sent < 0 || (size_t)sent != msg_len) {
        eprintf("tcp: send failed errno=%d\n", errno);
        close(fd);
        return NETTEST_ERR_TCP_SEND;
    }

    char buf[NETTEST_MAX_MSG];
    size_t off = 0;
    while (off < msg_len) {
        ssize_t got = recv(fd, buf + off, msg_len - off, 0);
        if (got <= 0) {
            eprintf("tcp: recv failed errno=%d\n", errno);
            close(fd);
            return NETTEST_ERR_TCP_RECV;
        }
        off += (size_t)got;
    }

    if (memcmp(buf, msg, msg_len) != 0) {
        eprintf("tcp: echo mismatch\n");
        close(fd);
        return NETTEST_ERR_TCP_MISMATCH;
    }

    close(fd);
    return NETTEST_OK;
}

static int udp_timeout_test(int timeout_ms)
{
    int fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (fd < 0) {
        eprintf("timeout: socket failed errno=%d\n", errno);
        return NETTEST_ERR_TIMEOUT_TEST;
    }

    if (set_socket_timeout(fd, timeout_ms) < 0) {
        eprintf("timeout: setsockopt failed errno=%d\n", errno);
        close(fd);
        return NETTEST_ERR_TIMEOUT_TEST;
    }

    char buf[1];
    struct sockaddr_in src = {0};
    socklen_t src_len = sizeof(src);
    ssize_t got = recvfrom(fd, buf, sizeof(buf), 0, (struct sockaddr *)&src, &src_len);
    int err = errno;
    close(fd);

    if (got >= 0) {
        eprintf("timeout: recv succeeded unexpectedly\n");
        return NETTEST_ERR_TIMEOUT_TEST;
    }
    if (err != ETIMEDOUT) {
        eprintf("timeout: recv errno=%d\n", err);
        return NETTEST_ERR_TIMEOUT_TEST;
    }
    return NETTEST_OK;
}

static int closed_fd_test(const char *host, int port, const char *msg)
{
    int fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (fd < 0) {
        eprintf("closed: socket failed errno=%d\n", errno);
        return NETTEST_ERR_CLOSED_FD;
    }

    struct sockaddr_in dst = {0};
    dst.sin_family = AF_INET;
    dst.sin_port = htons((uint16_t)port);
    if (inet_pton(AF_INET, host, &dst.sin_addr) != 1) {
        eprintf("closed: invalid address '%s'\n", host);
        close(fd);
        return NETTEST_ERR_CLOSED_FD;
    }

    close(fd);
    ssize_t sent = sendto(fd, msg, 1, 0, (struct sockaddr *)&dst, sizeof(dst));
    int err = errno;
    if (sent >= 0 || err != EBADF) {
        eprintf("closed: send expected EBADF got errno=%d\n", err);
        return NETTEST_ERR_CLOSED_FD;
    }
    return NETTEST_OK;
}

static int connect_invalid_test(void)
{
    int fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (fd < 0) {
        eprintf("neg: socket failed errno=%d\n", errno);
        return NETTEST_ERR_CONNECT_NEG;
    }

    struct sockaddr_in dst = {0};
    dst.sin_family = AF_INET;
    dst.sin_port = htons(1);
    dst.sin_addr.s_addr = htonl(INADDR_ANY);

    int rc = connect(fd, (struct sockaddr *)&dst, sizeof(dst));
    int err = errno;
    close(fd);

    if (rc == 0) {
        eprintf("neg: connect to 0.0.0.0 succeeded\n");
        return NETTEST_ERR_CONNECT_NEG;
    }
    if (err == 0) {
        eprintf("neg: connect failed with errno=0\n");
        return NETTEST_ERR_CONNECT_NEG;
    }
    return NETTEST_OK;
}

static int mixed_select_test(const char *udp_host,
                             int udp_port,
                             const char *tcp_host,
                             int tcp_port,
                             const char *msg,
                             int timeout_ms)
{
    int udp_fd = -1;
    int tcp_fd = -1;
    int rc = NETTEST_ERR_PARALLEL;

    udp_fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (udp_fd < 0) {
        eprintf("parallel: udp socket failed errno=%d\n", errno);
        goto done;
    }
    tcp_fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (tcp_fd < 0) {
        eprintf("parallel: tcp socket failed errno=%d\n", errno);
        goto done;
    }

    (void)set_socket_timeout(udp_fd, timeout_ms);
    (void)set_socket_timeout(tcp_fd, timeout_ms);

    struct sockaddr_in udp_dst = {0};
    udp_dst.sin_family = AF_INET;
    udp_dst.sin_port = htons((uint16_t)udp_port);
    if (inet_pton(AF_INET, udp_host, &udp_dst.sin_addr) != 1) {
        eprintf("parallel: invalid udp address '%s'\n", udp_host);
        goto done;
    }

    size_t msg_len = strlen(msg);
    if (sendto(udp_fd, msg, msg_len, 0, (struct sockaddr *)&udp_dst, sizeof(udp_dst)) < 0) {
        eprintf("parallel: udp send failed errno=%d\n", errno);
        goto done;
    }

    struct sockaddr_in tcp_dst = {0};
    tcp_dst.sin_family = AF_INET;
    tcp_dst.sin_port = htons((uint16_t)tcp_port);
    if (inet_pton(AF_INET, tcp_host, &tcp_dst.sin_addr) != 1) {
        eprintf("parallel: invalid tcp address '%s'\n", tcp_host);
        goto done;
    }
    if (connect(tcp_fd, (struct sockaddr *)&tcp_dst, sizeof(tcp_dst)) < 0) {
        eprintf("parallel: tcp connect failed errno=%d\n", errno);
        goto done;
    }
    if (send(tcp_fd, msg, msg_len, 0) < 0) {
        eprintf("parallel: tcp send failed errno=%d\n", errno);
        goto done;
    }

    bool udp_done = false;
    bool tcp_done = false;
    size_t tcp_off = 0;
    char udp_buf[NETTEST_MAX_MSG];
    char tcp_buf[NETTEST_MAX_MSG];
    uint64_t deadline = nettest_now_ms() + (uint64_t)timeout_ms;

    while (!udp_done || !tcp_done) {
        uint64_t now = nettest_now_ms();
        if (now >= deadline) {
            eprintf("parallel: timeout waiting for sockets\n");
            goto done;
        }
        uint64_t remaining = deadline - now;
        struct timeval tv = {
            .tv_sec = (time_t)(remaining / 1000ULL),
            .tv_usec = (suseconds_t)((remaining % 1000ULL) * 1000ULL),
        };

        fd_set rfds;
        FD_ZERO(&rfds);
        if (!udp_done) {
            FD_SET(udp_fd, &rfds);
        }
        if (!tcp_done) {
            FD_SET(tcp_fd, &rfds);
        }
        int max_fd = (udp_fd > tcp_fd) ? udp_fd : tcp_fd;

        int ready = select(max_fd + 1, &rfds, NULL, NULL, &tv);
        if (ready < 0) {
            eprintf("parallel: select failed errno=%d\n", errno);
            goto done;
        }
        if (ready == 0) {
            continue;
        }

        if (!udp_done && FD_ISSET(udp_fd, &rfds)) {
            struct sockaddr_in src = {0};
            socklen_t src_len = sizeof(src);
            ssize_t got = recvfrom(udp_fd, udp_buf, sizeof(udp_buf), 0,
                                   (struct sockaddr *)&src, &src_len);
            if (got < 0) {
                eprintf("parallel: udp recv failed errno=%d\n", errno);
                goto done;
            }
            if ((size_t)got != msg_len || memcmp(udp_buf, msg, msg_len) != 0) {
                eprintf("parallel: udp mismatch\n");
                goto done;
            }
            udp_done = true;
        }

        if (!tcp_done && FD_ISSET(tcp_fd, &rfds)) {
            ssize_t got = recv(tcp_fd, tcp_buf + tcp_off, msg_len - tcp_off, 0);
            if (got <= 0) {
                eprintf("parallel: tcp recv failed errno=%d\n", errno);
                goto done;
            }
            tcp_off += (size_t)got;
            if (tcp_off >= msg_len) {
                if (memcmp(tcp_buf, msg, msg_len) != 0) {
                    eprintf("parallel: tcp mismatch\n");
                    goto done;
                }
                tcp_done = true;
            }
        }
    }

    rc = NETTEST_OK;

done:
    if (udp_fd >= 0) {
        close(udp_fd);
    }
    if (tcp_fd >= 0) {
        close(tcp_fd);
    }
    return rc;
}

static int stress_open_close_test(int count)
{
    for (int i = 0; i < count; ++i) {
        int fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (fd < 0) {
            eprintf("stress: socket failed errno=%d\n", errno);
            return NETTEST_ERR_STRESS;
        }
        if (close(fd) < 0) {
            eprintf("stress: close failed errno=%d\n", errno);
            return NETTEST_ERR_STRESS;
        }
    }
    return NETTEST_OK;
}

typedef struct {
    int fd;
    int result;
    int err;
} close_thread_ctx_t;

static void *close_thread_main(void *arg)
{
    close_thread_ctx_t *ctx = (close_thread_ctx_t *)arg;
    if (ctx == NULL) {
        return NULL;
    }
    (void)usleep(NETTEST_CLOSE_DELAY_MS * 1000);
    ctx->result = close(ctx->fd);
    ctx->err = errno;
    return NULL;
}

static int close_during_io_test(int timeout_ms)
{
    int fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (fd < 0) {
        eprintf("close-io: socket failed errno=%d\n", errno);
        return NETTEST_ERR_CLOSE_IO;
    }
    if (set_socket_timeout(fd, timeout_ms) < 0) {
        eprintf("close-io: setsockopt failed errno=%d\n", errno);
        close(fd);
        return NETTEST_ERR_CLOSE_IO;
    }

    close_thread_ctx_t ctx = {
        .fd = fd,
        .result = -1,
        .err = 0,
    };
    pthread_t thread;
    if (pthread_create(&thread, NULL, close_thread_main, &ctx) != 0) {
        eprintf("close-io: pthread create failed\n");
        close(fd);
        return NETTEST_OK;
    }

    char buf[1];
    struct sockaddr_in src = {0};
    socklen_t src_len = sizeof(src);
    ssize_t got = recvfrom(fd, buf, sizeof(buf), 0, (struct sockaddr *)&src, &src_len);
    int recv_err = errno;

    (void)pthread_join(thread, NULL);

    if (ctx.result == 0) {
        if (got < 0 && recv_err != ETIMEDOUT) {
            return NETTEST_OK;
        }
        eprintf("close-io: recv rc=%ld errno=%d\n", (long)got, recv_err);
        return NETTEST_ERR_CLOSE_IO;
    }

    if (ctx.err != EBADF) {
        eprintf("close-io: close errno=%d\n", ctx.err);
        close(fd);
        return NETTEST_ERR_THREAD;
    }

    close(fd);
    return NETTEST_OK;
}

static int leak_socket_test(void)
{
    int fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (fd < 0) {
        eprintf("leak: socket failed errno=%d\n", errno);
        return NETTEST_ERR_LEAK;
    }
    eprintf("leak: leaving fd=%d open for job-exit cleanup\n", fd);
    return NETTEST_OK;
}

int main(int argc, char **argv)
{
    if (argc < 5) {
        usage(argv[0]);
        return NETTEST_ERR_USAGE;
    }

    const char *udp_host = argv[1];
    const char *udp_port_str = argv[2];
    const char *tcp_host = argv[3];
    const char *tcp_port_str = argv[4];
    const char *msg = NETTEST_DEFAULT_MSG;
    int timeout_ms = NETTEST_DEFAULT_TIMEOUT_MS;
    nettest_flags_t flags = {0};

    int udp_port = 0;
    int tcp_port = 0;
    if (!parse_port(udp_port_str, &udp_port) || !parse_port(tcp_port_str, &tcp_port)) {
        usage(argv[0]);
        return NETTEST_ERR_USAGE;
    }

    if (!parse_args(argc, argv, 5, &msg, &timeout_ms, &flags)) {
        usage(argv[0]);
        return NETTEST_ERR_USAGE;
    }

    if (timeout_ms == 0) {
        timeout_ms = NETTEST_DEFAULT_TIMEOUT_MS;
    }

    size_t msg_len = strlen(msg);
    if (msg_len == 0 || msg_len > NETTEST_MAX_MSG) {
        eprintf("msg length must be 1..%d\n", NETTEST_MAX_MSG);
        return NETTEST_ERR_USAGE;
    }

    int rc = udp_echo_test(udp_host, udp_port, msg, timeout_ms);
    if (rc != NETTEST_OK) {
        return rc;
    }

    rc = tcp_echo_test(tcp_host, tcp_port, msg, timeout_ms);
    if (rc != NETTEST_OK) {
        return rc;
    }

    if (flags.parallel) {
        rc = mixed_select_test(udp_host, udp_port, tcp_host, tcp_port, msg, timeout_ms);
        if (rc != NETTEST_OK) {
            return rc;
        }
    }

    if (flags.timeout) {
        rc = udp_timeout_test(timeout_ms);
        if (rc != NETTEST_OK) {
            return rc;
        }
    }

    if (flags.negative) {
        rc = closed_fd_test(udp_host, udp_port, msg);
        if (rc != NETTEST_OK) {
            return rc;
        }
        rc = connect_invalid_test();
        if (rc != NETTEST_OK) {
            return rc;
        }
        rc = close_during_io_test(timeout_ms);
        if (rc != NETTEST_OK) {
            return rc;
        }
    }

    if (flags.stress) {
        rc = stress_open_close_test(NETTEST_STRESS_SOCKETS);
        if (rc != NETTEST_OK) {
            return rc;
        }
    }

    if (flags.leak) {
        rc = leak_socket_test();
        if (rc != NETTEST_OK) {
            return rc;
        }
    }

    printf("nettest OK\n");
    return NETTEST_OK;
}
