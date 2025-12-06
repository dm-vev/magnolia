#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "sdkconfig.h"

#include "kernel/vfs/fs/devfs/devfs.h"
#include "kernel/vfs/fs/devfs/devfs_internal.h"
#include "kernel/vfs/fs/devfs/devfs_ioctl.h"
#include "kernel/vfs/fs/devfs/devfs_stream.h"

#ifndef CONFIG_MAGNOLIA_DEVFS_TTY_LINE_BUFFER_SIZE
#define CONFIG_MAGNOLIA_DEVFS_TTY_LINE_BUFFER_SIZE 256
#endif

#if CONFIG_MAGNOLIA_VFS_DEVFS

static const char *const STREAM_DEVICE_TAG = "devfs_stream_dev";

#if CONFIG_MAGNOLIA_DEVFS_PIPES
#define DEVFS_PIPE_PATH_FMT "/dev/pipe%zu"
#define DEVFS_PIPE_NAME_FMT "pipe%zu"

typedef struct {
    devfs_stream_context_t stream;
    char path[M_VFS_PATH_MAX_LEN];
    char name[M_VFS_NAME_MAX_LEN];
} devfs_pipe_device_t;

static devfs_pipe_device_t g_devfs_pipes[CONFIG_MAGNOLIA_DEVFS_PIPE_COUNT];

static m_vfs_error_t
devfs_pipe_read(void *private_data,
                void *buffer,
                size_t size,
                size_t *read)
{
    devfs_pipe_device_t *device = (devfs_pipe_device_t *)private_data;
    if (device == NULL) {
        return M_VFS_ERR_INVALID_PARAM;
    }
    return devfs_stream_try_read(&device->stream, buffer, size, read);
}

static m_vfs_error_t
devfs_pipe_write(void *private_data,
                 const void *buffer,
                 size_t size,
                 size_t *written)
{
    devfs_pipe_device_t *device = (devfs_pipe_device_t *)private_data;
    if (device == NULL) {
        return M_VFS_ERR_INVALID_PARAM;
    }
    return devfs_stream_try_write(&device->stream, buffer, size, written);
}

static uint32_t
devfs_pipe_poll(void *private_data)
{
    devfs_pipe_device_t *device = (devfs_pipe_device_t *)private_data;
    if (device == NULL) {
        return 0;
    }
    return devfs_stream_poll(&device->stream);
}

static m_vfs_error_t
devfs_pipe_get_info(void *private_data, devfs_device_info_t *info)
{
    devfs_pipe_device_t *device = (devfs_pipe_device_t *)private_data;
    if (device == NULL || info == NULL) {
        return M_VFS_ERR_INVALID_PARAM;
    }

    memset(info, 0, sizeof(*info));
    strncpy(info->path,
            device->path,
            sizeof(info->path));
    info->path[sizeof(info->path) - 1] = '\0';
    strncpy(info->name,
            device->name,
            sizeof(info->name));
    info->name[sizeof(info->name) - 1] = '\0';

    info->ready_mask = devfs_stream_ready_mask(&device->stream);
    devfs_shm_buffer_info_t buffer_info = {0};
    if (devfs_stream_buffer_info(&device->stream, &buffer_info) == M_VFS_ERR_OK) {
        info->shm_used = buffer_info.used;
        info->shm_capacity = buffer_info.capacity;
    }
    info->tty_echo = false;
    info->tty_canonical = false;
    return M_VFS_ERR_OK;
}

static m_vfs_error_t
devfs_pipe_ioctl(void *private_data,
                 unsigned long request,
                 void *arg)
{
    devfs_pipe_device_t *device = (devfs_pipe_device_t *)private_data;
    if (device == NULL) {
        return M_VFS_ERR_INVALID_PARAM;
    }

    switch (request) {
        case DEVFS_IOCTL_PIPE_RESET:
            return devfs_stream_control(&device->stream,
                                        IPC_SHM_CONTROL_RESET,
                                        NULL);
        case DEVFS_IOCTL_PIPE_GET_STATS:
            if (arg == NULL) {
                return M_VFS_ERR_INVALID_PARAM;
            }
            devfs_pipe_stats_t *stats = (devfs_pipe_stats_t *)arg;
            devfs_shm_buffer_info_t buffer_info = {0};
            m_vfs_error_t err = devfs_stream_buffer_info(&device->stream,
                                                         &buffer_info);
            if (err != M_VFS_ERR_OK) {
                return err;
            }
            stats->used = buffer_info.used;
            stats->capacity = buffer_info.capacity;
            return M_VFS_ERR_OK;
        default:
            return M_VFS_ERR_NOT_SUPPORTED;
    }
}

static void
devfs_pipe_attach_node(const devfs_entry_t *entry,
                       devfs_device_node_t *record)
{
    (void)entry;
    if (entry == NULL || record == NULL) {
        return;
    }

    devfs_pipe_device_t *device = (devfs_pipe_device_t *)entry->private_data;
    if (device == NULL) {
        return;
    }
    devfs_stream_attach_node(&device->stream, record->node);
}

static void
devfs_pipe_detach_node(const devfs_entry_t *entry,
                       devfs_device_node_t *record)
{
    (void)record;
    if (entry == NULL) {
        return;
    }
    devfs_pipe_device_t *device = (devfs_pipe_device_t *)entry->private_data;
    if (device == NULL) {
        return;
    }
    devfs_stream_detach_node(&device->stream);
}

static const devfs_ops_t s_devfs_pipe_ops = {
    .read = devfs_pipe_read,
    .write = devfs_pipe_write,
    .poll = devfs_pipe_poll,
    .ioctl = devfs_pipe_ioctl,
    .get_info = devfs_pipe_get_info,
};

bool
devfs_stream_register_pipes(void)
{
    bool success = true;
    for (size_t i = 0; i < CONFIG_MAGNOLIA_DEVFS_PIPE_COUNT; ++i) {
        devfs_pipe_device_t *device = &g_devfs_pipes[i];
        memset(device, 0, sizeof(*device));

        snprintf(device->path,
                 sizeof(device->path),
                 DEVFS_PIPE_PATH_FMT,
                 i);
        snprintf(device->name,
                 sizeof(device->name),
                 DEVFS_PIPE_NAME_FMT,
                 i);

        if (!devfs_stream_context_init(&device->stream,
                                       device->path,
                                       CONFIG_MAGNOLIA_DEVFS_SHM_BUFFER_SIZE,
                                       IPC_SHM_RING_OVERWRITE_BLOCK)) {
            ESP_LOGE(STREAM_DEVICE_TAG,
                     "Failed to init pipe %s",
                     device->path);
            success = false;
            continue;
        }

        if (devfs_register_ext(device->path,
                               &s_devfs_pipe_ops,
                               device,
                               devfs_pipe_attach_node,
                               devfs_pipe_detach_node) != M_VFS_ERR_OK) {
            ESP_LOGE(STREAM_DEVICE_TAG,
                     "Failed to register %s",
                     device->path);
            devfs_stream_context_cleanup(&device->stream);
            success = false;
        }
    }
    return success;
}
#endif /* CONFIG_MAGNOLIA_DEVFS_PIPES */

#if CONFIG_MAGNOLIA_DEVFS_TTY
#define DEVFS_TTY_PATH_FMT "/dev/tty%zu"
#define DEVFS_TTY_NAME_FMT "tty%zu"
#define DEVFS_TTY_LINE_BUFFER_SIZE CONFIG_MAGNOLIA_DEVFS_TTY_LINE_BUFFER_SIZE

typedef struct {
    devfs_stream_context_t stream;
    char path[M_VFS_PATH_MAX_LEN];
    char name[M_VFS_NAME_MAX_LEN];
    char line_buffer[DEVFS_TTY_LINE_BUFFER_SIZE];
    size_t line_len;
    bool line_ready;
    bool eof_pending;
    bool echo;
    bool canonical;
} devfs_tty_device_t;

static devfs_tty_device_t g_devfs_ttys[CONFIG_MAGNOLIA_DEVFS_TTY_COUNT];

static void
devfs_tty_reset(devfs_tty_device_t *device)
{
    if (device == NULL) {
        return;
    }
    device->line_len = 0;
    device->line_ready = false;
    device->eof_pending = false;
}

static size_t
devfs_tty_deliver(devfs_tty_device_t *device,
                  void *buffer,
                  size_t size)
{
    if (device == NULL || buffer == NULL || size == 0) {
        return 0;
    }

    size_t chunk = (device->line_len < size) ? device->line_len : size;
    memcpy(buffer, device->line_buffer, chunk);
    if (chunk < device->line_len) {
        memmove(device->line_buffer,
                device->line_buffer + chunk,
                device->line_len - chunk);
        device->line_len -= chunk;
    } else {
        devfs_tty_reset(device);
    }

    if (device->line_len == 0) {
        device->line_ready = false;
    }

    return chunk;
}

static void
devfs_tty_process_input(devfs_tty_device_t *device, const char *buffer, size_t size)
{
    if (device == NULL || buffer == NULL || size == 0) {
        return;
    }

    for (size_t i = 0; i < size; ++i) {
        char ch = buffer[i];
        if (ch == '\r') {
            ch = '\n';
        }
        if (ch == '\x04') {
            if (device->line_len == 0) {
                device->eof_pending = true;
                return;
            }
            device->line_ready = true;
            return;
        }
        if (ch == '\b' || ch == 0x7f) {
            if (device->line_len > 0) {
                --device->line_len;
            }
            continue;
        }
        if (device->line_len < sizeof(device->line_buffer)) {
            device->line_buffer[device->line_len++] = ch;
        }
        if (ch == '\n') {
            device->line_ready = true;
            return;
        }
    }
}

static m_vfs_error_t
devfs_tty_read(void *private_data,
               void *buffer,
               size_t size,
               size_t *read)
{
    devfs_tty_device_t *device = (devfs_tty_device_t *)private_data;
    if (device == NULL || buffer == NULL || read == NULL) {
        return M_VFS_ERR_INVALID_PARAM;
    }

    if (!device->canonical) {
        return devfs_stream_try_read(&device->stream, buffer, size, read);
    }

    if (device->line_ready && device->line_len > 0) {
        *read = devfs_tty_deliver(device, buffer, size);
        return M_VFS_ERR_OK;
    }

    if (device->eof_pending) {
        device->eof_pending = false;
        *read = 0;
        return M_VFS_ERR_OK;
    }

    while (!device->line_ready) {
        char tmp[64];
        size_t consumed = 0;
        m_vfs_error_t err = devfs_stream_try_read(&device->stream,
                                                  tmp,
                                                  sizeof(tmp),
                                                  &consumed);
        if (err == M_VFS_ERR_WOULD_BLOCK) {
            return M_VFS_ERR_WOULD_BLOCK;
        }
        if (err != M_VFS_ERR_OK) {
            return err;
        }
        devfs_tty_process_input(device, tmp, consumed);
    }

    *read = devfs_tty_deliver(device, buffer, size);
    return M_VFS_ERR_OK;
}

static m_vfs_error_t
devfs_tty_write(void *private_data,
                const void *buffer,
                size_t size,
                size_t *written)
{
    devfs_tty_device_t *device = (devfs_tty_device_t *)private_data;
    if (device == NULL || buffer == NULL || written == NULL) {
        return M_VFS_ERR_INVALID_PARAM;
    }
    if (size == 0) {
        *written = 0;
        return M_VFS_ERR_OK;
    }

    const uint8_t *src = buffer;
    size_t total = 0;
    char tmp[64];
    while (total < size) {
        size_t chunk = (size - total < sizeof(tmp)) ? (size - total) : sizeof(tmp);
        for (size_t i = 0; i < chunk; ++i) {
            uint8_t ch = src[total + i];
            tmp[i] = (ch == '\r') ? '\n' : (char)ch;
        }

        size_t written_chunk = 0;
        m_vfs_error_t err = devfs_stream_try_write(&device->stream,
                                                   tmp,
                                                   chunk,
                                                   &written_chunk);
        if (err == M_VFS_ERR_WOULD_BLOCK) {
            *written = total;
            return M_VFS_ERR_WOULD_BLOCK;
        }
        if (err != M_VFS_ERR_OK) {
            *written = total;
            return err;
        }
        total += written_chunk;
    }

    *written = total;
    return M_VFS_ERR_OK;
}

static uint32_t
devfs_tty_poll(void *private_data)
{
    devfs_tty_device_t *device = (devfs_tty_device_t *)private_data;
    if (device == NULL) {
        return 0;
    }
    return devfs_stream_poll(&device->stream);
}

static m_vfs_error_t
devfs_tty_flush(devfs_tty_device_t *device)
{
    if (device == NULL) {
        return M_VFS_ERR_INVALID_PARAM;
    }

    devfs_tty_reset(device);
    return devfs_stream_control(&device->stream, IPC_SHM_CONTROL_FLUSH, NULL);
}

static m_vfs_error_t
devfs_tty_get_info(void *private_data, devfs_device_info_t *info)
{
    devfs_tty_device_t *device = (devfs_tty_device_t *)private_data;
    if (device == NULL || info == NULL) {
        return M_VFS_ERR_INVALID_PARAM;
    }

    memset(info, 0, sizeof(*info));
    strncpy(info->path,
            device->path,
            sizeof(info->path));
    info->path[sizeof(info->path) - 1] = '\0';
    strncpy(info->name,
            device->name,
            sizeof(info->name));
    info->name[sizeof(info->name) - 1] = '\0';

    info->ready_mask = devfs_stream_ready_mask(&device->stream);
    devfs_shm_buffer_info_t buffer_info = {0};
    if (devfs_stream_buffer_info(&device->stream, &buffer_info) == M_VFS_ERR_OK) {
        info->shm_used = buffer_info.used;
        info->shm_capacity = buffer_info.capacity;
    }
    info->tty_echo = device->echo;
    info->tty_canonical = device->canonical;
    return M_VFS_ERR_OK;
}

static m_vfs_error_t
devfs_tty_ioctl(void *private_data,
                unsigned long request,
                void *arg)
{
    devfs_tty_device_t *device = (devfs_tty_device_t *)private_data;
    if (device == NULL) {
        return M_VFS_ERR_INVALID_PARAM;
    }

    switch (request) {
        case DEVFS_IOCTL_TTY_SET_MODE:
            if (arg == NULL) {
                return M_VFS_ERR_INVALID_PARAM;
            }
            devfs_tty_mode_t *mode = (devfs_tty_mode_t *)arg;
            device->echo = mode->echo;
            device->canonical = mode->canonical;
            devfs_tty_reset(device);
            return M_VFS_ERR_OK;
        case DEVFS_IOCTL_TTY_GET_MODE:
            if (arg == NULL) {
                return M_VFS_ERR_INVALID_PARAM;
            }
            devfs_tty_mode_t *out_mode = (devfs_tty_mode_t *)arg;
            out_mode->echo = device->echo;
            out_mode->canonical = device->canonical;
            return M_VFS_ERR_OK;
        case DEVFS_IOCTL_TTY_FLUSH:
            return devfs_tty_flush(device);
        case DEVFS_IOCTL_TTY_SET_ECHO:
            if (arg == NULL) {
                return M_VFS_ERR_INVALID_PARAM;
            }
            device->echo = (*(bool *)arg);
            return M_VFS_ERR_OK;
        case DEVFS_IOCTL_TTY_GET_ECHO:
            if (arg == NULL) {
                return M_VFS_ERR_INVALID_PARAM;
            }
            *(bool *)arg = device->echo;
            return M_VFS_ERR_OK;
        case DEVFS_IOCTL_TTY_SET_CANON:
            if (arg == NULL) {
                return M_VFS_ERR_INVALID_PARAM;
            }
            device->canonical = (*(bool *)arg);
            devfs_tty_reset(device);
            return M_VFS_ERR_OK;
        case DEVFS_IOCTL_TTY_GET_CANON:
            if (arg == NULL) {
                return M_VFS_ERR_INVALID_PARAM;
            }
            *(bool *)arg = device->canonical;
            return M_VFS_ERR_OK;
        default:
            return M_VFS_ERR_NOT_SUPPORTED;
    }
}

static void
devfs_tty_attach_node(const devfs_entry_t *entry,
                      devfs_device_node_t *record)
{
    (void)entry;
    if (entry == NULL || record == NULL) {
        return;
    }

    devfs_tty_device_t *device = (devfs_tty_device_t *)entry->private_data;
    if (device == NULL) {
        return;
    }
    devfs_stream_attach_node(&device->stream, record->node);
}

static void
devfs_tty_detach_node(const devfs_entry_t *entry,
                      devfs_device_node_t *record)
{
    (void)record;
    if (entry == NULL) {
        return;
    }
    devfs_tty_device_t *device = (devfs_tty_device_t *)entry->private_data;
    if (device == NULL) {
        return;
    }
    devfs_stream_detach_node(&device->stream);
}

static const devfs_ops_t s_devfs_tty_ops = {
    .read = devfs_tty_read,
    .write = devfs_tty_write,
    .poll = devfs_tty_poll,
    .ioctl = devfs_tty_ioctl,
    .get_info = devfs_tty_get_info,
};

bool
devfs_stream_register_ttys(void)
{
    bool success = true;
    for (size_t i = 0; i < CONFIG_MAGNOLIA_DEVFS_TTY_COUNT; ++i) {
        devfs_tty_device_t *device = &g_devfs_ttys[i];
        memset(device, 0, sizeof(*device));

        snprintf(device->path,
                 sizeof(device->path),
                 DEVFS_TTY_PATH_FMT,
                 i);
        snprintf(device->name,
                 sizeof(device->name),
                 DEVFS_TTY_NAME_FMT,
                 i);

        device->echo = CONFIG_MAGNOLIA_DEVFS_TTY_ECHO;
        device->canonical = CONFIG_MAGNOLIA_DEVFS_TTY_CANON;

        if (!devfs_stream_context_init(&device->stream,
                                       device->path,
                                       CONFIG_MAGNOLIA_DEVFS_SHM_BUFFER_SIZE,
                                       IPC_SHM_RING_OVERWRITE_BLOCK)) {
            ESP_LOGE(STREAM_DEVICE_TAG,
                     "Failed to init tty %s",
                     device->path);
            success = false;
            continue;
        }

        devfs_tty_reset(device);

        if (devfs_register_ext(device->path,
                               &s_devfs_tty_ops,
                               device,
                               devfs_tty_attach_node,
                               devfs_tty_detach_node) != M_VFS_ERR_OK) {
            ESP_LOGE(STREAM_DEVICE_TAG,
                     "Failed to register %s",
                     device->path);
            devfs_stream_context_cleanup(&device->stream);
            success = false;
        }
    }
    return success;
}
#endif /* CONFIG_MAGNOLIA_DEVFS_TTY */

#if CONFIG_MAGNOLIA_DEVFS_PTY
#define DEVFS_PTY_MASTER_PATH_FMT "/dev/pty/master%zu"
#define DEVFS_PTY_SLAVE_PATH_FMT "/dev/pty/slave%zu"
#define DEVFS_PTY_MASTER_NAME_FMT "pty/master%zu"
#define DEVFS_PTY_SLAVE_NAME_FMT "pty/slave%zu"

typedef struct devfs_pty_pair devfs_pty_pair_t;

typedef struct {
    devfs_pty_pair_t *pair;
    char path[M_VFS_PATH_MAX_LEN];
    char name[M_VFS_NAME_MAX_LEN];
    bool master;
} devfs_pty_endpoint_t;

typedef struct devfs_pty_pair {
    devfs_stream_context_t master_to_slave;
    devfs_stream_context_t slave_to_master;
    char master_path[M_VFS_PATH_MAX_LEN];
    char master_name[M_VFS_NAME_MAX_LEN];
    char slave_path[M_VFS_PATH_MAX_LEN];
    char slave_name[M_VFS_NAME_MAX_LEN];
    char slave_line_buffer[DEVFS_TTY_LINE_BUFFER_SIZE];
    size_t slave_line_len;
    bool slave_line_ready;
    bool slave_eof_pending;
    bool slave_echo;
    bool slave_canonical;
    bool master_open;
    bool slave_open;
} devfs_pty_pair_t;

static devfs_pty_pair_t g_devfs_pty_pairs[CONFIG_MAGNOLIA_DEVFS_PTY_COUNT];
static devfs_pty_endpoint_t g_devfs_pty_masters[CONFIG_MAGNOLIA_DEVFS_PTY_COUNT];
static devfs_pty_endpoint_t g_devfs_pty_slaves[CONFIG_MAGNOLIA_DEVFS_PTY_COUNT];

static void
devfs_pty_slave_reset(devfs_pty_pair_t *pair)
{
    if (pair == NULL) {
        return;
    }
    pair->slave_line_len = 0;
    pair->slave_line_ready = false;
    pair->slave_eof_pending = false;
}

static size_t
devfs_pty_slave_deliver(devfs_pty_pair_t *pair,
                        void *buffer,
                        size_t size)
{
    if (pair == NULL || buffer == NULL || size == 0) {
        return 0;
    }

    size_t chunk = (pair->slave_line_len < size) ? pair->slave_line_len : size;
    memcpy(buffer, pair->slave_line_buffer, chunk);
    if (chunk < pair->slave_line_len) {
        memmove(pair->slave_line_buffer,
                pair->slave_line_buffer + chunk,
                pair->slave_line_len - chunk);
        pair->slave_line_len -= chunk;
    } else {
        devfs_pty_slave_reset(pair);
    }

    if (pair->slave_line_len == 0) {
        pair->slave_line_ready = false;
    }

    return chunk;
}

static void
devfs_pty_slave_process_input(devfs_pty_pair_t *pair,
                              const char *buffer,
                              size_t size)
{
    if (pair == NULL || buffer == NULL || size == 0) {
        return;
    }

    for (size_t i = 0; i < size; ++i) {
        char ch = buffer[i];
        if (ch == '\r') {
            ch = '\n';
        }
        if (ch == '\x04') {
            if (pair->slave_line_len == 0) {
                pair->slave_eof_pending = true;
                return;
            }
            pair->slave_line_ready = true;
            return;
        }
        if (ch == '\b' || ch == 0x7f) {
            if (pair->slave_line_len > 0) {
                --pair->slave_line_len;
            }
            continue;
        }
        if (pair->slave_line_len < sizeof(pair->slave_line_buffer)) {
            pair->slave_line_buffer[pair->slave_line_len++] = ch;
        }
        if (ch == '\n') {
            pair->slave_line_ready = true;
            return;
        }
    }
}

static void
devfs_pty_signal_hangup(devfs_stream_context_t *ctx)
{
    if (ctx == NULL || ctx->node == NULL) {
        return;
    }
    devfs_notify(ctx->node, DEVFS_EVENT_HANGUP);
}

static void
devfs_pty_master_attach_node(const devfs_entry_t *entry,
                             devfs_device_node_t *record)
{
    (void)entry;
    if (entry == NULL || record == NULL) {
        return;
    }

    devfs_pty_endpoint_t *endpoint = (devfs_pty_endpoint_t *)entry->private_data;
    if (endpoint == NULL || endpoint->pair == NULL) {
        return;
    }
    devfs_stream_attach_node(&endpoint->pair->slave_to_master, record->node);
}

static void
devfs_pty_master_detach_node(const devfs_entry_t *entry,
                             devfs_device_node_t *record)
{
    (void)record;
    if (entry == NULL) {
        return;
    }
    devfs_pty_endpoint_t *endpoint = (devfs_pty_endpoint_t *)entry->private_data;
    if (endpoint == NULL || endpoint->pair == NULL) {
        return;
    }
    devfs_stream_detach_node(&endpoint->pair->slave_to_master);
}

static void
devfs_pty_slave_attach_node(const devfs_entry_t *entry,
                            devfs_device_node_t *record)
{
    (void)entry;
    if (entry == NULL || record == NULL) {
        return;
    }
    devfs_pty_endpoint_t *endpoint = (devfs_pty_endpoint_t *)entry->private_data;
    if (endpoint == NULL || endpoint->pair == NULL) {
        return;
    }
    devfs_stream_attach_node(&endpoint->pair->master_to_slave, record->node);
}

static void
devfs_pty_slave_detach_node(const devfs_entry_t *entry,
                            devfs_device_node_t *record)
{
    (void)record;
    if (entry == NULL) {
        return;
    }
    devfs_pty_endpoint_t *endpoint = (devfs_pty_endpoint_t *)entry->private_data;
    if (endpoint == NULL || endpoint->pair == NULL) {
        return;
    }
    devfs_stream_detach_node(&endpoint->pair->master_to_slave);
}

static m_vfs_error_t
devfs_pty_master_read(void *private_data,
                      void *buffer,
                      size_t size,
                      size_t *read)
{
    devfs_pty_endpoint_t *endpoint = (devfs_pty_endpoint_t *)private_data;
    if (endpoint == NULL || endpoint->pair == NULL || read == NULL || buffer == NULL) {
        return M_VFS_ERR_INVALID_PARAM;
    }
    return devfs_stream_try_read(&endpoint->pair->slave_to_master,
                                buffer,
                                size,
                                read);
}

static m_vfs_error_t
devfs_pty_master_write(void *private_data,
                       const void *buffer,
                       size_t size,
                       size_t *written)
{
    devfs_pty_endpoint_t *endpoint = (devfs_pty_endpoint_t *)private_data;
    if (endpoint == NULL || endpoint->pair == NULL || written == NULL || buffer == NULL) {
        return M_VFS_ERR_INVALID_PARAM;
    }
    return devfs_stream_try_write(&endpoint->pair->master_to_slave,
                                 buffer,
                                 size,
                                 written);
}

static m_vfs_error_t
devfs_pty_master_close(void *private_data)
{
    devfs_pty_endpoint_t *endpoint = (devfs_pty_endpoint_t *)private_data;
    if (endpoint == NULL || endpoint->pair == NULL) {
        return M_VFS_ERR_INVALID_PARAM;
    }
    devfs_pty_pair_t *pair = endpoint->pair;
    if (!pair->master_open) {
        return M_VFS_ERR_OK;
    }
    pair->master_open = false;
    devfs_stream_control(&pair->master_to_slave,
                         IPC_SHM_CONTROL_NOTIFY_READERS,
                         NULL);
    devfs_pty_signal_hangup(&pair->master_to_slave);
    return M_VFS_ERR_OK;
}

static m_vfs_error_t
devfs_pty_slave_read(void *private_data,
                     void *buffer,
                     size_t size,
                     size_t *read)
{
    devfs_pty_endpoint_t *endpoint = (devfs_pty_endpoint_t *)private_data;
    if (endpoint == NULL || endpoint->pair == NULL || read == NULL || buffer == NULL) {
        return M_VFS_ERR_INVALID_PARAM;
    }

    devfs_pty_pair_t *pair = endpoint->pair;
    if (!pair->slave_canonical) {
        return devfs_stream_try_read(&pair->master_to_slave,
                                    buffer,
                                    size,
                                    read);
    }

    if (pair->slave_line_ready && pair->slave_line_len > 0) {
        *read = devfs_pty_slave_deliver(pair, buffer, size);
        return M_VFS_ERR_OK;
    }

    if (pair->slave_eof_pending) {
        pair->slave_eof_pending = false;
        *read = 0;
        return M_VFS_ERR_OK;
    }

    while (!pair->slave_line_ready) {
        char tmp[64];
        size_t consumed = 0;
        m_vfs_error_t err = devfs_stream_try_read(&pair->master_to_slave,
                                                  tmp,
                                                  sizeof(tmp),
                                                  &consumed);
        if (err == M_VFS_ERR_WOULD_BLOCK) {
            return M_VFS_ERR_WOULD_BLOCK;
        }
        if (err != M_VFS_ERR_OK) {
            return err;
        }
        devfs_pty_slave_process_input(pair, tmp, consumed);
    }

    *read = devfs_pty_slave_deliver(pair, buffer, size);
    return M_VFS_ERR_OK;
}

static m_vfs_error_t
devfs_pty_slave_write(void *private_data,
                      const void *buffer,
                      size_t size,
                      size_t *written)
{
    devfs_pty_endpoint_t *endpoint = (devfs_pty_endpoint_t *)private_data;
    if (endpoint == NULL || endpoint->pair == NULL || written == NULL || buffer == NULL) {
        return M_VFS_ERR_INVALID_PARAM;
    }
    return devfs_stream_try_write(&endpoint->pair->slave_to_master,
                                  buffer,
                                  size,
                                  written);
}

static m_vfs_error_t
devfs_pty_slave_close(void *private_data)
{
    devfs_pty_endpoint_t *endpoint = (devfs_pty_endpoint_t *)private_data;
    if (endpoint == NULL || endpoint->pair == NULL) {
        return M_VFS_ERR_INVALID_PARAM;
    }
    devfs_pty_pair_t *pair = endpoint->pair;
    if (!pair->slave_open) {
        return M_VFS_ERR_OK;
    }
    pair->slave_open = false;
    devfs_stream_control(&pair->slave_to_master,
                         IPC_SHM_CONTROL_NOTIFY_READERS,
                         NULL);
    devfs_pty_signal_hangup(&pair->slave_to_master);
    return M_VFS_ERR_OK;
}

static uint32_t
devfs_pty_master_poll(void *private_data)
{
    devfs_pty_endpoint_t *endpoint = (devfs_pty_endpoint_t *)private_data;
    if (endpoint == NULL || endpoint->pair == NULL) {
        return 0;
    }
    devfs_event_mask_t mask = 0;
    mask |= devfs_stream_ready_mask(&endpoint->pair->slave_to_master) &
            DEVFS_EVENT_READABLE;
    mask |= devfs_stream_ready_mask(&endpoint->pair->master_to_slave) &
            DEVFS_EVENT_WRITABLE;
    return mask;
}

static uint32_t
devfs_pty_slave_poll(void *private_data)
{
    devfs_pty_endpoint_t *endpoint = (devfs_pty_endpoint_t *)private_data;
    if (endpoint == NULL || endpoint->pair == NULL) {
        return 0;
    }
    devfs_event_mask_t mask = 0;
    mask |= devfs_stream_ready_mask(&endpoint->pair->master_to_slave) &
            DEVFS_EVENT_READABLE;
    mask |= devfs_stream_ready_mask(&endpoint->pair->slave_to_master) &
            DEVFS_EVENT_WRITABLE;
    return mask;
}

static m_vfs_error_t
devfs_pty_master_get_info(void *private_data,
                          devfs_device_info_t *info)
{
    devfs_pty_endpoint_t *endpoint = (devfs_pty_endpoint_t *)private_data;
    if (endpoint == NULL || endpoint->pair == NULL || info == NULL) {
        return M_VFS_ERR_INVALID_PARAM;
    }

    devfs_pty_pair_t *pair = endpoint->pair;
    memset(info, 0, sizeof(*info));
    strncpy(info->path,
            endpoint->path,
            sizeof(info->path));
    info->path[sizeof(info->path) - 1] = '\0';
    strncpy(info->name,
            endpoint->name,
            sizeof(info->name));
    info->name[sizeof(info->name) - 1] = '\0';

    info->ready_mask = devfs_pty_master_poll(private_data);
    devfs_shm_buffer_info_t info_master = {0};
    devfs_shm_buffer_info_t info_slave = {0};
    devfs_stream_buffer_info(&pair->master_to_slave, &info_master);
    devfs_stream_buffer_info(&pair->slave_to_master, &info_slave);
    info->shm_used = info_master.used + info_slave.used;
    info->shm_capacity = info_master.capacity + info_slave.capacity;
    info->pty_is_slave = false;
    strncpy(info->pty_peer,
            pair->slave_path,
            sizeof(info->pty_peer));
    info->pty_peer[sizeof(info->pty_peer) - 1] = '\0';
    return M_VFS_ERR_OK;
}

static m_vfs_error_t
devfs_pty_slave_get_info(void *private_data,
                         devfs_device_info_t *info)
{
    devfs_pty_endpoint_t *endpoint = (devfs_pty_endpoint_t *)private_data;
    if (endpoint == NULL || endpoint->pair == NULL || info == NULL) {
        return M_VFS_ERR_INVALID_PARAM;
    }

    devfs_pty_pair_t *pair = endpoint->pair;
    memset(info, 0, sizeof(*info));
    strncpy(info->path,
            endpoint->path,
            sizeof(info->path));
    info->path[sizeof(info->path) - 1] = '\0';
    strncpy(info->name,
            endpoint->name,
            sizeof(info->name));
    info->name[sizeof(info->name) - 1] = '\0';

    info->ready_mask = devfs_pty_slave_poll(private_data);
    devfs_shm_buffer_info_t info_master = {0};
    devfs_shm_buffer_info_t info_slave = {0};
    devfs_stream_buffer_info(&pair->master_to_slave, &info_master);
    devfs_stream_buffer_info(&pair->slave_to_master, &info_slave);
    info->shm_used = info_master.used + info_slave.used;
    info->shm_capacity = info_master.capacity + info_slave.capacity;
    info->pty_is_slave = true;
    strncpy(info->pty_peer,
            pair->master_path,
            sizeof(info->pty_peer));
    info->pty_peer[sizeof(info->pty_peer) - 1] = '\0';
    return M_VFS_ERR_OK;
}

static m_vfs_error_t
devfs_pty_slave_ioctl(void *private_data,
                      unsigned long request,
                      void *arg)
{
    devfs_pty_endpoint_t *endpoint = (devfs_pty_endpoint_t *)private_data;
    if (endpoint == NULL || endpoint->pair == NULL) {
        return M_VFS_ERR_INVALID_PARAM;
    }

    devfs_pty_pair_t *pair = endpoint->pair;
    switch (request) {
        case DEVFS_IOCTL_TTY_SET_MODE:
            if (arg == NULL) {
                return M_VFS_ERR_INVALID_PARAM;
            }
            devfs_tty_mode_t *mode = (devfs_tty_mode_t *)arg;
            pair->slave_echo = mode->echo;
            pair->slave_canonical = mode->canonical;
            devfs_pty_slave_reset(pair);
            return M_VFS_ERR_OK;
        case DEVFS_IOCTL_TTY_GET_MODE:
            if (arg == NULL) {
                return M_VFS_ERR_INVALID_PARAM;
            }
            devfs_tty_mode_t *out_mode = (devfs_tty_mode_t *)arg;
            out_mode->echo = pair->slave_echo;
            out_mode->canonical = pair->slave_canonical;
            return M_VFS_ERR_OK;
        case DEVFS_IOCTL_TTY_FLUSH:
            devfs_pty_slave_reset(pair);
            return devfs_stream_control(&pair->master_to_slave,
                                        IPC_SHM_CONTROL_FLUSH,
                                        NULL);
        case DEVFS_IOCTL_TTY_SET_ECHO:
            if (arg == NULL) {
                return M_VFS_ERR_INVALID_PARAM;
            }
            pair->slave_echo = (*(bool *)arg);
            return M_VFS_ERR_OK;
        case DEVFS_IOCTL_TTY_GET_ECHO:
            if (arg == NULL) {
                return M_VFS_ERR_INVALID_PARAM;
            }
            *(bool *)arg = pair->slave_echo;
            return M_VFS_ERR_OK;
        case DEVFS_IOCTL_TTY_SET_CANON:
            if (arg == NULL) {
                return M_VFS_ERR_INVALID_PARAM;
            }
            pair->slave_canonical = (*(bool *)arg);
            devfs_pty_slave_reset(pair);
            return M_VFS_ERR_OK;
        case DEVFS_IOCTL_TTY_GET_CANON:
            if (arg == NULL) {
                return M_VFS_ERR_INVALID_PARAM;
            }
            *(bool *)arg = pair->slave_canonical;
            return M_VFS_ERR_OK;
        default:
            return M_VFS_ERR_NOT_SUPPORTED;
    }
}

static m_vfs_error_t
devfs_pty_master_ioctl(void *private_data,
                       unsigned long request,
                       void *arg)
{
    (void)arg;
    devfs_pty_endpoint_t *endpoint = (devfs_pty_endpoint_t *)private_data;
    if (endpoint == NULL || endpoint->pair == NULL) {
        return M_VFS_ERR_INVALID_PARAM;
    }

    if (request == DEVFS_IOCTL_PTY_HANGUP) {
        devfs_pty_pair_t *pair = endpoint->pair;
        devfs_stream_control(&pair->master_to_slave,
                             IPC_SHM_CONTROL_NOTIFY_READERS,
                             NULL);
        devfs_pty_signal_hangup(&pair->master_to_slave);
        return M_VFS_ERR_OK;
    }
    return M_VFS_ERR_NOT_SUPPORTED;
}

static const devfs_ops_t s_devfs_pty_master_ops = {
    .read = devfs_pty_master_read,
    .write = devfs_pty_master_write,
    .poll = devfs_pty_master_poll,
    .close = devfs_pty_master_close,
    .ioctl = devfs_pty_master_ioctl,
    .get_info = devfs_pty_master_get_info,
};

static const devfs_ops_t s_devfs_pty_slave_ops = {
    .read = devfs_pty_slave_read,
    .write = devfs_pty_slave_write,
    .poll = devfs_pty_slave_poll,
    .close = devfs_pty_slave_close,
    .ioctl = devfs_pty_slave_ioctl,
    .get_info = devfs_pty_slave_get_info,
};

bool
devfs_stream_register_ptys(void)
{
    bool success = true;
    for (size_t i = 0; i < CONFIG_MAGNOLIA_DEVFS_PTY_COUNT; ++i) {
        devfs_pty_pair_t *pair = &g_devfs_pty_pairs[i];
        memset(pair, 0, sizeof(*pair));
        pair->master_open = true;
        pair->slave_open = true;
        pair->slave_echo = CONFIG_MAGNOLIA_DEVFS_TTY_ECHO;
        pair->slave_canonical = CONFIG_MAGNOLIA_DEVFS_TTY_CANON;

        snprintf(pair->master_path,
                 sizeof(pair->master_path),
                 DEVFS_PTY_MASTER_PATH_FMT,
                 i);
        snprintf(pair->master_name,
                 sizeof(pair->master_name),
                 DEVFS_PTY_MASTER_NAME_FMT,
                 i);
        snprintf(pair->slave_path,
                 sizeof(pair->slave_path),
                 DEVFS_PTY_SLAVE_PATH_FMT,
                 i);
        snprintf(pair->slave_name,
                 sizeof(pair->slave_name),
                 DEVFS_PTY_SLAVE_NAME_FMT,
                 i);

        if (!devfs_stream_context_init(&pair->master_to_slave,
                                       pair->slave_path,
                                       CONFIG_MAGNOLIA_DEVFS_SHM_BUFFER_SIZE,
                                       IPC_SHM_RING_OVERWRITE_BLOCK)) {
            ESP_LOGE(STREAM_DEVICE_TAG,
                     "Failed to init pty master->slave %s",
                     pair->slave_path);
            success = false;
            continue;
        }

        if (!devfs_stream_context_init(&pair->slave_to_master,
                                       pair->master_path,
                                       CONFIG_MAGNOLIA_DEVFS_SHM_BUFFER_SIZE,
                                       IPC_SHM_RING_OVERWRITE_BLOCK)) {
            ESP_LOGE(STREAM_DEVICE_TAG,
                     "Failed to init pty slave->master %s",
                     pair->master_path);
            devfs_stream_context_cleanup(&pair->master_to_slave);
            success = false;
            continue;
        }

        devfs_pty_slave_reset(pair);

        devfs_pty_endpoint_t *master = &g_devfs_pty_masters[i];
        master->pair = pair;
        master->master = true;
        strncpy(master->path, pair->master_path, sizeof(master->path));
        strncpy(master->name, pair->master_name, sizeof(master->name));

        if (devfs_register_ext(master->path,
                               &s_devfs_pty_master_ops,
                               master,
                               devfs_pty_master_attach_node,
                               devfs_pty_master_detach_node) != M_VFS_ERR_OK) {
            ESP_LOGE(STREAM_DEVICE_TAG,
                     "Failed to register %s",
                     master->path);
            devfs_stream_context_cleanup(&pair->master_to_slave);
            devfs_stream_context_cleanup(&pair->slave_to_master);
            success = false;
            continue;
        }

        devfs_pty_endpoint_t *slave = &g_devfs_pty_slaves[i];
        slave->pair = pair;
        slave->master = false;
        strncpy(slave->path, pair->slave_path, sizeof(slave->path));
        strncpy(slave->name, pair->slave_name, sizeof(slave->name));

        if (devfs_register_ext(slave->path,
                               &s_devfs_pty_slave_ops,
                               slave,
                               devfs_pty_slave_attach_node,
                               devfs_pty_slave_detach_node) != M_VFS_ERR_OK) {
            ESP_LOGE(STREAM_DEVICE_TAG,
                     "Failed to register %s",
                     slave->path);
            devfs_unregister(master->path);
            devfs_stream_context_cleanup(&pair->master_to_slave);
            devfs_stream_context_cleanup(&pair->slave_to_master);
            success = false;
            continue;
        }
    }
    return success;
}
#endif /* CONFIG_MAGNOLIA_DEVFS_PTY */

#endif /* CONFIG_MAGNOLIA_VFS_DEVFS */
