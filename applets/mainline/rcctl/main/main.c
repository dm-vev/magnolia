#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/ioctl.h>

#define DEVFS_IOCTL_PIPE_GET_STATS 0x21

typedef struct {
    size_t used;
    size_t capacity;
} devfs_pipe_stats_t;

typedef struct {
    uint32_t magic;
    uint32_t type;
    uint32_t id;
    uint32_t status;
    uint32_t length;
} rc_msg_header_t;

enum {
    RC_MSG_MAGIC = 0x52434431u,
    RC_MSG_TYPE_REQUEST = 1,
    RC_MSG_TYPE_RESPONSE = 2,
    RC_MSG_MAX = 512,
    RC_MSG_HEADER_SIZE = sizeof(rc_msg_header_t),
    RC_MSG_MAX_PAYLOAD = RC_MSG_MAX - RC_MSG_HEADER_SIZE,
    RC_RESPONSE_TIMEOUT_MS = 2000,
};

static void rc_usage(void)
{
    const char *msg =
        "usage: rcctl start <service>\n"
        "       rcctl stop <service>\n"
        "       rcctl restart <service>\n"
        "       rcctl status <service>\n"
        "       rcctl status all\n"
        "       rcctl reload\n";
    (void)write(STDERR_FILENO, msg, strlen(msg));
}

static uint64_t rc_now_ms(void)
{
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        return 0;
    }
    return (uint64_t)ts.tv_sec * 1000u + (uint64_t)ts.tv_nsec / 1000000u;
}

static bool rc_pipe_stats(int fd, devfs_pipe_stats_t *stats)
{
    if (ioctl(fd, DEVFS_IOCTL_PIPE_GET_STATS, stats) != 0) {
        return false;
    }
    return true;
}

static bool rc_pipe_read_message(int fd, rc_msg_header_t *hdr,
                                 char *payload, size_t payload_cap)
{
    devfs_pipe_stats_t stats;
    if (!rc_pipe_stats(fd, &stats)) {
        return false;
    }
    if (stats.used < RC_MSG_HEADER_SIZE) {
        return false;
    }

    ssize_t r = read(fd, hdr, RC_MSG_HEADER_SIZE);
    if (r != (ssize_t)RC_MSG_HEADER_SIZE) {
        return false;
    }
    if (hdr->magic != RC_MSG_MAGIC) {
        return false;
    }
    if (hdr->length > payload_cap) {
        return false;
    }
    if (hdr->length == 0) {
        payload[0] = '\0';
        return true;
    }

    if (!rc_pipe_stats(fd, &stats) || stats.used < hdr->length) {
        return false;
    }
    r = read(fd, payload, hdr->length);
    if (r != (ssize_t)hdr->length) {
        return false;
    }
    payload[hdr->length] = '\0';
    return true;
}

static bool rc_pipe_write_message(int fd, uint32_t type, uint32_t id,
                                  const char *payload)
{
    size_t len = payload ? strlen(payload) : 0;
    if (len > RC_MSG_MAX_PAYLOAD) {
        len = RC_MSG_MAX_PAYLOAD;
    }

    uint8_t buffer[RC_MSG_MAX];
    rc_msg_header_t hdr = {
        .magic = RC_MSG_MAGIC,
        .type = type,
        .id = id,
        .status = 0,
        .length = (uint32_t)len,
    };
    memcpy(buffer, &hdr, RC_MSG_HEADER_SIZE);
    if (len > 0) {
        memcpy(buffer + RC_MSG_HEADER_SIZE, payload, len);
    }

    size_t total = RC_MSG_HEADER_SIZE + len;
    for (int attempt = 0; attempt < 20; ++attempt) {
        devfs_pipe_stats_t stats;
        if (rc_pipe_stats(fd, &stats)) {
            if (stats.capacity >= total && stats.capacity - stats.used >= total) {
                ssize_t w = write(fd, buffer, total);
                if (w == (ssize_t)total) {
                    return true;
                }
            }
        }
        usleep(50 * 1000);
    }
    return false;
}

static bool rc_wait_response(int fd, uint32_t id,
                             char *payload, size_t payload_cap,
                             uint32_t *out_status)
{
    uint64_t deadline = rc_now_ms() + RC_RESPONSE_TIMEOUT_MS;
    while (rc_now_ms() < deadline) {
        rc_msg_header_t hdr;
        if (rc_pipe_read_message(fd, &hdr, payload, payload_cap)) {
            if (hdr.type != RC_MSG_TYPE_RESPONSE) {
                continue;
            }
            if (hdr.id != id) {
                continue;
            }
            *out_status = hdr.status;
            return true;
        }
        usleep(50 * 1000);
    }
    return false;
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        rc_usage();
        return 2;
    }

    const char *cmd = argv[1];
    char request[RC_MSG_MAX_PAYLOAD + 1];

    if (strcmp(cmd, "reload") == 0) {
        snprintf(request, sizeof(request), "reload");
    } else if (strcmp(cmd, "status") == 0) {
        if (argc < 3) {
            rc_usage();
            return 2;
        }
        snprintf(request, sizeof(request), "status %s", argv[2]);
    } else if (strcmp(cmd, "start") == 0 || strcmp(cmd, "stop") == 0 ||
               strcmp(cmd, "restart") == 0) {
        if (argc < 3) {
            rc_usage();
            return 2;
        }
        snprintf(request, sizeof(request), "%s %s", cmd, argv[2]);
    } else {
        rc_usage();
        return 2;
    }

    int req_fd = open("/dev/pipe0", O_RDWR);
    if (req_fd < 0) {
        return 4;
    }
    int resp_fd = open("/dev/pipe1", O_RDWR);
    if (resp_fd < 0) {
        return 4;
    }

    uint32_t id = (uint32_t)(rc_now_ms() & 0xffffffffu);
    if (!rc_pipe_write_message(req_fd, RC_MSG_TYPE_REQUEST, id, request)) {
        return 4;
    }

    char response[RC_MSG_MAX_PAYLOAD + 1];
    uint32_t status = 4;
    if (!rc_wait_response(resp_fd, id, response, RC_MSG_MAX_PAYLOAD, &status)) {
        return 4;
    }

    if (response[0] != '\0') {
        (void)write(STDOUT_FILENO, response, strlen(response));
        (void)write(STDOUT_FILENO, "\n", 1);
    }

    if (status > 4) {
        status = 1;
    }
    return (int)status;
}
