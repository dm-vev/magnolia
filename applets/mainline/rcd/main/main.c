#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/stat.h>

int m_elf_run_file(const char *path, int argc, char *argv[], int *out_rc);

typedef struct m_job_queue m_job_queue_t;
typedef struct m_job_handle m_job_handle_t;
typedef m_job_handle_t *m_job_id_t;

typedef enum {
    M_JOB_RESULT_SUCCESS = 0,
    M_JOB_RESULT_ERROR,
    M_JOB_RESULT_CANCELLED,
} m_job_result_status_t;

typedef struct {
    m_job_result_status_t status;
    const void *payload;
    size_t payload_size;
} m_job_result_descriptor_t;

typedef m_job_result_descriptor_t (*m_job_handler_t)(m_job_id_t job, void *data);

typedef enum {
    M_JOB_OK = 0,
    M_JOB_ERR_INVALID_PARAM,
    M_JOB_ERR_INVALID_HANDLE,
    M_JOB_ERR_NO_MEMORY,
    M_JOB_ERR_QUEUE_FULL,
    M_JOB_ERR_TIMEOUT,
    M_JOB_ERR_DESTROYED,
    M_JOB_ERR_STATE,
    M_JOB_ERR_SHUTDOWN,
    M_JOB_ERR_NOT_READY,
    M_JOB_ERR_BUSY,
} m_job_error_t;

typedef enum {
    M_JOB_FUTURE_WAIT_OK = 0,
    M_JOB_FUTURE_WAIT_TIMEOUT,
    M_JOB_FUTURE_WAIT_NOT_READY,
    M_JOB_FUTURE_WAIT_DESTROYED,
    M_JOB_FUTURE_WAIT_SHUTDOWN,
} m_job_future_wait_result_t;

typedef struct {
    const char *name;
    size_t capacity;
    size_t worker_count;
    size_t stack_depth;
    unsigned int priority;
    bool debug_log;
} m_job_queue_config_t;

m_job_queue_t *m_job_queue_create(const m_job_queue_config_t *config);
m_job_error_t m_job_queue_destroy(m_job_queue_t *queue);
m_job_error_t m_job_queue_submit_with_handle(m_job_queue_t *queue,
                                             m_job_handler_t handler,
                                             void *data,
                                             m_job_handle_t **out_handle);
m_job_error_t m_job_cancel(m_job_id_t job);
m_job_error_t m_job_handle_destroy(m_job_id_t job);
m_job_future_wait_result_t m_job_try_wait_for_job(m_job_id_t job,
                                                  m_job_result_descriptor_t *result);

#define DEVFS_IOCTL_PIPE_RESET 0x20
#define DEVFS_IOCTL_PIPE_GET_STATS 0x21

typedef struct {
    size_t used;
    size_t capacity;
} devfs_pipe_stats_t;

enum {
    RC_MAX_SERVICES = 32,
    RC_NAME_MAX = 32,
    RC_PATH_MAX = 128,
    RC_ARGS_MAX = 128,
    RC_LINE_MAX = 256,
    RC_MAX_ARGV = 16,
    RC_STOP_TIMEOUT_MS = 2000,
    RC_DEFAULT_BACKOFF_MS = 1000,
    RC_DEFAULT_RESTART_LIMIT = 5,
    RC_DEFAULT_RESTART_WINDOW_MS = 60000,
    RC_JOB_STACK_DEPTH = 8192,
    RC_JOB_PRIORITY = 3,
    RC_LOOP_SLEEP_MS = 100,
};

typedef enum {
    RC_SERVICE_STOPPED = 0,
    RC_SERVICE_STARTING,
    RC_SERVICE_RUNNING,
    RC_SERVICE_STOPPING,
    RC_SERVICE_FAILED,
} rc_service_state_t;

typedef enum {
    RC_RESTART_NEVER = 0,
    RC_RESTART_ON_FAILURE,
    RC_RESTART_ALWAYS,
} rc_restart_policy_t;

typedef struct {
    char name[RC_NAME_MAX];
    char path[RC_PATH_MAX];
    char args[RC_ARGS_MAX];
    bool autostart;
    rc_restart_policy_t restart;
    uint32_t backoff_ms;
    uint32_t restart_limit;
    uint32_t restart_window_ms;

    rc_service_state_t state;
    bool stop_requested;
    bool restart_after_stop;
    uint64_t stop_deadline_ms;
    bool pending_restart;
    uint64_t next_restart_ms;
    uint64_t restart_window_start_ms;
    uint32_t restart_count;
    m_job_queue_t *queue;
    m_job_id_t job;
    int last_exit_code;
    int last_run_ret;
} rc_service_t;

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
};

static void rc_log(const char *fmt, ...)
{
    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (n <= 0) {
        return;
    }
    if ((size_t)n >= sizeof(buf)) {
        n = (int)sizeof(buf) - 1;
    }
    printf("[RC] %.*s\n", n, buf);
}

static uint64_t rc_now_ms(void)
{
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        return 0;
    }
    return (uint64_t)ts.tv_sec * 1000u + (uint64_t)ts.tv_nsec / 1000000u;
}

static const char *rc_state_name(rc_service_state_t state)
{
    switch (state) {
    case RC_SERVICE_STOPPED:
        return "STOPPED";
    case RC_SERVICE_STARTING:
        return "STARTING";
    case RC_SERVICE_RUNNING:
        return "RUNNING";
    case RC_SERVICE_STOPPING:
        return "STOPPING";
    case RC_SERVICE_FAILED:
        return "FAILED";
    default:
        return "UNKNOWN";
    }
}

static const char *rc_restart_name(rc_restart_policy_t policy)
{
    switch (policy) {
    case RC_RESTART_NEVER:
        return "never";
    case RC_RESTART_ON_FAILURE:
        return "on-failure";
    case RC_RESTART_ALWAYS:
        return "always";
    default:
        return "unknown";
    }
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
        rc_log("rcd: pipe read header failed");
        return false;
    }
    if (hdr->magic != RC_MSG_MAGIC) {
        rc_log("rcd: bad message magic");
        return false;
    }
    if (hdr->length > payload_cap) {
        rc_log("rcd: message payload too large");
        return false;
    }
    if (hdr->length == 0) {
        payload[0] = '\0';
        return true;
    }

    if (!rc_pipe_stats(fd, &stats) || stats.used < hdr->length) {
        rc_log("rcd: message payload incomplete");
        return false;
    }
    r = read(fd, payload, hdr->length);
    if (r != (ssize_t)hdr->length) {
        rc_log("rcd: pipe read payload failed");
        return false;
    }
    payload[hdr->length] = '\0';
    return true;
}

static bool rc_pipe_write_message(int fd, uint32_t type, uint32_t id,
                                  uint32_t status, const char *payload)
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
        .status = status,
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

static char *rc_trim(char *s)
{
    while (*s == ' ' || *s == '\t' || *s == '\r' || *s == '\n') {
        ++s;
    }
    size_t len = strlen(s);
    while (len > 0) {
        char c = s[len - 1];
        if (c != ' ' && c != '\t' && c != '\r' && c != '\n') {
            break;
        }
        s[len - 1] = '\0';
        len--;
    }
    return s;
}

static void rc_copy(char *dst, size_t dst_len, const char *src)
{
    if (dst == NULL || dst_len == 0) {
        return;
    }
    if (src == NULL) {
        dst[0] = '\0';
        return;
    }
    strncpy(dst, src, dst_len - 1);
    dst[dst_len - 1] = '\0';
}

static bool rc_parse_bool(const char *value)
{
    if (value == NULL) {
        return false;
    }
    if (strcmp(value, "1") == 0 || strcmp(value, "yes") == 0 ||
        strcmp(value, "true") == 0 || strcmp(value, "on") == 0) {
        return true;
    }
    return false;
}

static uint32_t rc_parse_u32(const char *value, uint32_t fallback)
{
    if (value == NULL || *value == '\0') {
        return fallback;
    }
    char *end = NULL;
    unsigned long v = strtoul(value, &end, 10);
    if (end == value) {
        return fallback;
    }
    if (v > UINT32_MAX) {
        return fallback;
    }
    return (uint32_t)v;
}

static rc_restart_policy_t rc_parse_restart(const char *value)
{
    if (value == NULL) {
        return RC_RESTART_NEVER;
    }
    if (strcmp(value, "always") == 0) {
        return RC_RESTART_ALWAYS;
    }
    if (strcmp(value, "on-failure") == 0 || strcmp(value, "on_failure") == 0) {
        return RC_RESTART_ON_FAILURE;
    }
    return RC_RESTART_NEVER;
}

static void rc_service_defaults(rc_service_t *svc)
{
    memset(svc, 0, sizeof(*svc));
    svc->autostart = false;
    svc->restart = RC_RESTART_NEVER;
    svc->backoff_ms = RC_DEFAULT_BACKOFF_MS;
    svc->restart_limit = RC_DEFAULT_RESTART_LIMIT;
    svc->restart_window_ms = RC_DEFAULT_RESTART_WINDOW_MS;
    svc->state = RC_SERVICE_STOPPED;
}

static bool rc_service_valid(const rc_service_t *svc)
{
    return svc->name[0] != '\0' && svc->path[0] != '\0';
}

static void rc_config_finalize(rc_service_t *current,
                               rc_service_t *services,
                               size_t *count)
{
    if (!rc_service_valid(current)) {
        if (current->name[0] != '\0' && current->path[0] == '\0') {
            rc_log("rcd: config %s missing path", current->name);
        }
        return;
    }
    if (*count >= RC_MAX_SERVICES) {
        rc_log("rcd: too many services, skipping %s", current->name);
        return;
    }
    services[*count] = *current;
    services[*count].state = RC_SERVICE_STOPPED;
    (*count)++;
}

static void rc_apply_kv(rc_service_t *svc, const char *key, const char *val)
{
    if (strcmp(key, "path") == 0) {
        rc_copy(svc->path, sizeof(svc->path), val);
    } else if (strcmp(key, "args") == 0) {
        rc_copy(svc->args, sizeof(svc->args), val);
    } else if (strcmp(key, "autostart") == 0) {
        svc->autostart = rc_parse_bool(val);
    } else if (strcmp(key, "restart") == 0) {
        svc->restart = rc_parse_restart(val);
    } else if (strcmp(key, "backoff") == 0 || strcmp(key, "backoff_ms") == 0) {
        svc->backoff_ms = rc_parse_u32(val, RC_DEFAULT_BACKOFF_MS);
    } else if (strcmp(key, "restart_limit") == 0) {
        svc->restart_limit = rc_parse_u32(val, RC_DEFAULT_RESTART_LIMIT);
    } else if (strcmp(key, "restart_window_ms") == 0 ||
               strcmp(key, "restart_window") == 0) {
        svc->restart_window_ms = rc_parse_u32(val, RC_DEFAULT_RESTART_WINDOW_MS);
    }
}

static void rc_parse_config_file(const char *path,
                                 rc_service_t *services,
                                 size_t *count)
{
    int fd = open(path, 0);
    if (fd < 0) {
        rc_log("rcd: config %s open failed errno=%d", path, errno);
        return;
    }

    rc_service_t current;
    rc_service_defaults(&current);

    char line[RC_LINE_MAX];
    size_t line_len = 0;
    while (1) {
        char buf[64];
        ssize_t r = read(fd, buf, sizeof(buf));
        if (r <= 0) {
            break;
        }
        for (ssize_t i = 0; i < r; ++i) {
            char c = buf[i];
            if (c == '\n') {
                line[line_len] = '\0';
                line_len = 0;
                char *hash = strchr(line, '#');
                if (hash) {
                    *hash = '\0';
                }
                char *trimmed = rc_trim(line);
                if (*trimmed == '\0') {
                    continue;
                }
                char *eq = strchr(trimmed, '=');
                if (!eq) {
                    continue;
                }
                *eq = '\0';
                char *key = rc_trim(trimmed);
                char *val = rc_trim(eq + 1);
                if (strcmp(key, "service") == 0) {
                    rc_config_finalize(&current, services, count);
                    rc_service_defaults(&current);
                    rc_copy(current.name, sizeof(current.name), val);
                } else {
                    rc_apply_kv(&current, key, val);
                }
            } else if (line_len + 1 < sizeof(line)) {
                line[line_len++] = c;
            }
        }
    }

    if (line_len > 0) {
        line[line_len] = '\0';
        char *hash = strchr(line, '#');
        if (hash) {
            *hash = '\0';
        }
        char *trimmed = rc_trim(line);
        if (*trimmed != '\0') {
            char *eq = strchr(trimmed, '=');
            if (eq) {
                *eq = '\0';
                char *key = rc_trim(trimmed);
                char *val = rc_trim(eq + 1);
                if (strcmp(key, "service") == 0) {
                    rc_config_finalize(&current, services, count);
                    rc_service_defaults(&current);
                    rc_copy(current.name, sizeof(current.name), val);
                } else {
                    rc_apply_kv(&current, key, val);
                }
            }
        }
    }

    rc_config_finalize(&current, services, count);
    (void)close(fd);
}

static void rc_apply_overrides(rc_service_t *services, size_t count)
{
    for (size_t i = 0; i < count; ++i) {
        char path[128];
        int n = snprintf(path, sizeof(path), "/etc/rc.d/%s.conf", services[i].name);
        if (n <= 0 || (size_t)n >= sizeof(path)) {
            continue;
        }
        int fd = open(path, 0);
        if (fd < 0) {
            continue;
        }
        char line[RC_LINE_MAX];
        size_t line_len = 0;
        while (1) {
            char buf[64];
            ssize_t r = read(fd, buf, sizeof(buf));
            if (r <= 0) {
                break;
            }
            for (ssize_t j = 0; j < r; ++j) {
                char c = buf[j];
                if (c == '\n') {
                    line[line_len] = '\0';
                    line_len = 0;
                    char *hash = strchr(line, '#');
                    if (hash) {
                        *hash = '\0';
                    }
                    char *trimmed = rc_trim(line);
                    if (*trimmed == '\0') {
                        continue;
                    }
                    char *eq = strchr(trimmed, '=');
                    if (!eq) {
                        continue;
                    }
                    *eq = '\0';
                    char *key = rc_trim(trimmed);
                    char *val = rc_trim(eq + 1);
                    if (strcmp(key, "service") != 0) {
                        rc_apply_kv(&services[i], key, val);
                    }
                } else if (line_len + 1 < sizeof(line)) {
                    line[line_len++] = c;
                }
            }
        }
        (void)close(fd);
    }
}

static rc_service_t *rc_find_service(rc_service_t *services, size_t count, const char *name)
{
    for (size_t i = 0; i < count; ++i) {
        if (strcmp(services[i].name, name) == 0) {
            return &services[i];
        }
    }
    return NULL;
}

static int rc_build_argv(const rc_service_t *svc, char *argv[], size_t max_args)
{
    char args_copy[RC_ARGS_MAX];
    strncpy(args_copy, svc->args, sizeof(args_copy) - 1);
    args_copy[sizeof(args_copy) - 1] = '\0';

    size_t argc = 0;
    argv[argc++] = (char *)svc->name;
    char *save = NULL;
    char *token = strtok_r(args_copy, " \t", &save);
    while (token != NULL && argc + 1 < max_args) {
        argv[argc++] = token;
        token = strtok_r(NULL, " \t", &save);
    }
    argv[argc] = NULL;
    return (int)argc;
}

static m_job_result_descriptor_t rc_service_job(m_job_id_t job, void *data)
{
    (void)job;
    rc_service_t *svc = (rc_service_t *)data;
    if (svc == NULL) {
        return (m_job_result_descriptor_t){
            .status = M_JOB_RESULT_ERROR,
            .payload = NULL,
            .payload_size = 0,
        };
    }

    char *argv[RC_MAX_ARGV];
    int argc = rc_build_argv(svc, argv, RC_MAX_ARGV);
    int rc = 0;
    int ret = m_elf_run_file(svc->path, argc, argv, &rc);
    svc->last_exit_code = rc;
    svc->last_run_ret = ret;
    if (ret == 0 && rc == 0) {
        return (m_job_result_descriptor_t){
            .status = M_JOB_RESULT_SUCCESS,
            .payload = NULL,
            .payload_size = 0,
        };
    }
    return (m_job_result_descriptor_t){
        .status = M_JOB_RESULT_ERROR,
        .payload = NULL,
        .payload_size = 0,
    };
}

static bool rc_start_service(rc_service_t *svc)
{
    if (svc == NULL || !rc_service_valid(svc)) {
        return false;
    }
    if (svc->job != NULL || svc->state == RC_SERVICE_RUNNING ||
        svc->state == RC_SERVICE_STARTING) {
        return true;
    }

    svc->state = RC_SERVICE_STARTING;
    m_job_queue_config_t cfg = {
        .name = svc->name,
        .capacity = 1,
        .worker_count = 1,
        .stack_depth = RC_JOB_STACK_DEPTH,
        .priority = RC_JOB_PRIORITY,
        .debug_log = false,
    };
    svc->queue = m_job_queue_create(&cfg);
    if (svc->queue == NULL) {
        svc->state = RC_SERVICE_FAILED;
        return false;
    }

    svc->job = NULL;
    if (m_job_queue_submit_with_handle(svc->queue, rc_service_job, svc, &svc->job) != M_JOB_OK ||
        svc->job == NULL) {
        m_job_queue_destroy(svc->queue);
        svc->queue = NULL;
        svc->state = RC_SERVICE_FAILED;
        return false;
    }

    svc->pending_restart = false;
    svc->stop_requested = false;
    svc->restart_after_stop = false;
    svc->state = RC_SERVICE_RUNNING;
    rc_log("service %s started job=%p", svc->name, (void *)svc->job);
    return true;
}

static void rc_force_stop(rc_service_t *svc)
{
    if (svc == NULL) {
        return;
    }
    if (svc->job != NULL) {
        (void)m_job_cancel(svc->job);
    }
    if (svc->queue != NULL) {
        (void)m_job_queue_destroy(svc->queue);
        svc->queue = NULL;
    }
    if (svc->job != NULL) {
        (void)m_job_handle_destroy(svc->job);
        svc->job = NULL;
    }
    svc->state = RC_SERVICE_STOPPED;
    svc->stop_requested = false;
    svc->pending_restart = false;
}

static bool rc_stop_service(rc_service_t *svc, bool restart_after)
{
    if (svc == NULL) {
        return false;
    }
    if (svc->job == NULL) {
        svc->state = RC_SERVICE_STOPPED;
        if (restart_after) {
            return rc_start_service(svc);
        }
        return true;
    }

    svc->stop_requested = true;
    svc->restart_after_stop = restart_after;
    svc->state = RC_SERVICE_STOPPING;
    svc->stop_deadline_ms = rc_now_ms() + RC_STOP_TIMEOUT_MS;
    (void)m_job_cancel(svc->job);
    rc_log("service %s stopping job=%p", svc->name, (void *)svc->job);
    return true;
}

static bool rc_should_restart(const rc_service_t *svc, bool success)
{
    if (svc == NULL) {
        return false;
    }
    if (svc->restart == RC_RESTART_ALWAYS) {
        return true;
    }
    if (svc->restart == RC_RESTART_ON_FAILURE) {
        return !success;
    }
    return false;
}

static bool rc_schedule_restart(rc_service_t *svc, uint64_t now_ms)
{
    if (svc == NULL) {
        return false;
    }
    if (svc->restart_limit > 0 && svc->restart_window_ms > 0) {
        if (svc->restart_window_start_ms == 0 ||
            now_ms - svc->restart_window_start_ms > svc->restart_window_ms) {
            svc->restart_window_start_ms = now_ms;
            svc->restart_count = 0;
        }
        if (svc->restart_count >= svc->restart_limit) {
            rc_log("service %s restart limit reached", svc->name);
            svc->pending_restart = false;
            svc->state = RC_SERVICE_FAILED;
            return false;
        }
        svc->restart_count++;
    }
    svc->pending_restart = true;
    svc->next_restart_ms = now_ms + svc->backoff_ms;
    rc_log("service %s restarting (policy=%s)", svc->name, rc_restart_name(svc->restart));
    return true;
}

static void rc_handle_job_exit(rc_service_t *svc, uint64_t now_ms)
{
    if (svc == NULL || svc->job == NULL) {
        return;
    }
    m_job_result_descriptor_t result = {0};
    m_job_future_wait_result_t wait = m_job_try_wait_for_job(svc->job, &result);
    if (wait != M_JOB_FUTURE_WAIT_OK) {
        return;
    }

    bool success = (result.status == M_JOB_RESULT_SUCCESS &&
                    svc->last_run_ret == 0 && svc->last_exit_code == 0);
    rc_log("service %s exited rc=%d", svc->name, svc->last_exit_code);

    if (svc->queue != NULL) {
        (void)m_job_queue_destroy(svc->queue);
        svc->queue = NULL;
    }
    if (svc->job != NULL) {
        (void)m_job_handle_destroy(svc->job);
        svc->job = NULL;
    }

    bool stop_requested = svc->stop_requested;
    bool restart_after_stop = svc->restart_after_stop;
    svc->stop_requested = false;
    svc->restart_after_stop = false;

    if (stop_requested) {
        svc->state = RC_SERVICE_STOPPED;
        if (restart_after_stop) {
            (void)rc_start_service(svc);
        }
        return;
    }

    svc->state = success ? RC_SERVICE_STOPPED : RC_SERVICE_FAILED;
    if (rc_should_restart(svc, success)) {
        (void)rc_schedule_restart(svc, now_ms);
    }
}

static void rc_poll_service(rc_service_t *svc, uint64_t now_ms)
{
    if (svc == NULL) {
        return;
    }

    if (svc->job != NULL) {
        if (svc->stop_requested && now_ms >= svc->stop_deadline_ms) {
            rc_log("service %s stop timeout, killing", svc->name);
            rc_force_stop(svc);
            if (svc->restart_after_stop) {
                svc->restart_after_stop = false;
                (void)rc_start_service(svc);
            }
            return;
        }
        rc_handle_job_exit(svc, now_ms);
    }

    if (svc->job == NULL && svc->pending_restart &&
        now_ms >= svc->next_restart_ms) {
        svc->pending_restart = false;
        (void)rc_start_service(svc);
    }
}

static void rc_format_service_status(const rc_service_t *svc,
                                     char *buf, size_t buf_len)
{
    if (svc == NULL || buf_len == 0) {
        return;
    }
    if (svc->state == RC_SERVICE_RUNNING) {
        snprintf(buf, buf_len, "%s %s job=%p",
                 svc->name, rc_state_name(svc->state), (void *)svc->job);
        return;
    }
    if (svc->state == RC_SERVICE_FAILED) {
        snprintf(buf, buf_len, "%s %s rc=%d",
                 svc->name, rc_state_name(svc->state), svc->last_exit_code);
        return;
    }
    snprintf(buf, buf_len, "%s %s", svc->name, rc_state_name(svc->state));
}

static void rc_handle_request(rc_service_t *services, size_t *count,
                              const rc_msg_header_t *hdr, char *payload,
                              int resp_fd)
{
    char *save = NULL;
    char *cmd = strtok_r(payload, " \t", &save);
    char *arg = strtok_r(NULL, " \t", &save);

    if (cmd == NULL) {
        (void)rc_pipe_write_message(resp_fd, RC_MSG_TYPE_RESPONSE,
                                    hdr->id, 2, "invalid command");
        return;
    }

    if (strcmp(cmd, "status") == 0) {
        if (arg == NULL) {
            (void)rc_pipe_write_message(resp_fd, RC_MSG_TYPE_RESPONSE,
                                        hdr->id, 2, "missing service");
            return;
        }
        if (strcmp(arg, "all") == 0) {
            char out[RC_MSG_MAX_PAYLOAD + 1];
            size_t used = 0;
            for (size_t i = 0; i < *count; ++i) {
                char line[96];
                rc_format_service_status(&services[i], line, sizeof(line));
                size_t line_len = strlen(line);
                if (used + line_len + 1 >= sizeof(out)) {
                    const char *tail = "\n...";
                    size_t tail_len = strlen(tail);
                    if (used + tail_len + 1 < sizeof(out)) {
                        memcpy(out + used, tail, tail_len);
                        used += tail_len;
                        out[used] = '\0';
                    }
                    break;
                }
                memcpy(out + used, line, line_len);
                used += line_len;
                out[used++] = '\n';
                out[used] = '\0';
            }
            if (used == 0) {
                strncpy(out, "no services", sizeof(out) - 1);
                out[sizeof(out) - 1] = '\0';
            }
            (void)rc_pipe_write_message(resp_fd, RC_MSG_TYPE_RESPONSE,
                                        hdr->id, 0, out);
            return;
        }
        rc_service_t *svc = rc_find_service(services, *count, arg);
        if (svc == NULL) {
            (void)rc_pipe_write_message(resp_fd, RC_MSG_TYPE_RESPONSE,
                                        hdr->id, 1, "service not found");
            return;
        }
        char out[96];
        rc_format_service_status(svc, out, sizeof(out));
        uint32_t status = (svc->state == RC_SERVICE_FAILED) ? 3 : 0;
        (void)rc_pipe_write_message(resp_fd, RC_MSG_TYPE_RESPONSE,
                                    hdr->id, status, out);
        return;
    }

    if (strcmp(cmd, "reload") == 0) {
        size_t new_count = 0;
        rc_service_t fresh[RC_MAX_SERVICES];
        rc_parse_config_file("/etc/rc.conf", fresh, &new_count);
        rc_apply_overrides(fresh, new_count);

        for (size_t i = 0; i < new_count; ++i) {
        rc_service_t *existing = rc_find_service(services, *count, fresh[i].name);
            if (existing != NULL) {
                rc_copy(existing->path, sizeof(existing->path), fresh[i].path);
                rc_copy(existing->args, sizeof(existing->args), fresh[i].args);
                existing->autostart = fresh[i].autostart;
                existing->restart = fresh[i].restart;
                existing->backoff_ms = fresh[i].backoff_ms;
                existing->restart_limit = fresh[i].restart_limit;
                existing->restart_window_ms = fresh[i].restart_window_ms;
            } else if (*count < RC_MAX_SERVICES) {
                services[*count] = fresh[i];
                (*count)++;
            }
        }
        (void)rc_pipe_write_message(resp_fd, RC_MSG_TYPE_RESPONSE,
                                    hdr->id, 0, "reloaded");
        return;
    }

    if (arg == NULL) {
        (void)rc_pipe_write_message(resp_fd, RC_MSG_TYPE_RESPONSE,
                                    hdr->id, 2, "missing service");
        return;
    }

    rc_service_t *svc = rc_find_service(services, *count, arg);
    if (svc == NULL) {
        (void)rc_pipe_write_message(resp_fd, RC_MSG_TYPE_RESPONSE,
                                    hdr->id, 1, "service not found");
        return;
    }

    if (strcmp(cmd, "start") == 0) {
        if (rc_start_service(svc)) {
            (void)rc_pipe_write_message(resp_fd, RC_MSG_TYPE_RESPONSE,
                                        hdr->id, 0, "started");
        } else {
            (void)rc_pipe_write_message(resp_fd, RC_MSG_TYPE_RESPONSE,
                                        hdr->id, 3, "start failed");
        }
        return;
    }

    if (strcmp(cmd, "stop") == 0) {
        (void)rc_stop_service(svc, false);
        (void)rc_pipe_write_message(resp_fd, RC_MSG_TYPE_RESPONSE,
                                    hdr->id, 0, "stopping");
        return;
    }

    if (strcmp(cmd, "restart") == 0) {
        (void)rc_stop_service(svc, true);
        (void)rc_pipe_write_message(resp_fd, RC_MSG_TYPE_RESPONSE,
                                    hdr->id, 0, "restarting");
        return;
    }

    (void)rc_pipe_write_message(resp_fd, RC_MSG_TYPE_RESPONSE,
                                hdr->id, 2, "invalid command");
}

static void rc_write_ready_file(void)
{
    (void)mkdir("/var", 0755);
    (void)mkdir("/var/run", 0755);
    int fd = open("/var/run/rcd.ready", O_CREAT | O_TRUNC | O_WRONLY, 0644);
    if (fd < 0) {
        rc_log("rcd: ready file create failed errno=%d", errno);
        return;
    }
    const char *msg = "ready\n";
    (void)write(fd, msg, strlen(msg));
    (void)close(fd);
}

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    rc_log("rcd start");

    int req_fd = open("/dev/pipe0", O_RDWR);
    if (req_fd < 0) {
        rc_log("rcd: open /dev/pipe0 failed errno=%d", errno);
        return 1;
    }
    int resp_fd = open("/dev/pipe1", O_RDWR);
    if (resp_fd < 0) {
        rc_log("rcd: open /dev/pipe1 failed errno=%d", errno);
        return 1;
    }

    (void)ioctl(req_fd, DEVFS_IOCTL_PIPE_RESET, NULL);
    (void)ioctl(resp_fd, DEVFS_IOCTL_PIPE_RESET, NULL);

    rc_service_t services[RC_MAX_SERVICES];
    size_t count = 0;
    rc_parse_config_file("/etc/rc.conf", services, &count);
    rc_apply_overrides(services, count);

    rc_write_ready_file();

    while (1) {
        char payload[RC_MSG_MAX_PAYLOAD + 1];
        rc_msg_header_t hdr;

        while (rc_pipe_read_message(req_fd, &hdr, payload, RC_MSG_MAX_PAYLOAD)) {
            if (hdr.type != RC_MSG_TYPE_REQUEST) {
                continue;
            }
            rc_handle_request(services, &count, &hdr, payload, resp_fd);
        }

        uint64_t now_ms = rc_now_ms();
        for (size_t i = 0; i < count; ++i) {
            rc_poll_service(&services[i], now_ms);
        }

        usleep(RC_LOOP_SLEEP_MS * 1000);
    }
}
