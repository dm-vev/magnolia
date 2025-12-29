#include "kernel/core/log/m_klog.h"

#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "sdkconfig.h"
#include "esp_log_write.h"
#include "kernel/vfs/fs/devfs/devfs.h"
#include "kernel/vfs/fs/devfs/devfs_internal.h"
#include "kernel/vfs/fs/devfs/devfs_stream.h"

#ifndef CONFIG_MAGNOLIA_DEVFS_KMSG
#define CONFIG_MAGNOLIA_DEVFS_KMSG 0
#endif

#ifndef CONFIG_MAGNOLIA_DEVFS_KMSG_BUFFER_SIZE
#define CONFIG_MAGNOLIA_DEVFS_KMSG_BUFFER_SIZE 4096
#endif

#ifndef CONFIG_MAGNOLIA_KLOG_LINE_MAX
#define CONFIG_MAGNOLIA_KLOG_LINE_MAX 256
#endif

#if CONFIG_MAGNOLIA_DEVFS_KMSG && CONFIG_MAGNOLIA_VFS_DEVFS && CONFIG_MAGNOLIA_IPC_ENABLED

static const char s_klog_path[] = "/dev/kmsg";

static devfs_stream_context_t s_klog_stream;
static bool s_klog_ready;
static vprintf_like_t s_prev_vprintf;

static void m_klog_stream_write(const char *data, size_t len)
{
    if (!s_klog_ready || data == NULL || len == 0) {
        return;
    }
    size_t written = 0;
    (void)devfs_stream_try_write(&s_klog_stream, data, len, &written);
}

static int m_klog_vprintf(const char *fmt, va_list ap)
{
    if (fmt != NULL) {
        char buffer[CONFIG_MAGNOLIA_KLOG_LINE_MAX];
        va_list copy;
        va_copy(copy, ap);
        int written = vsnprintf(buffer, sizeof(buffer), fmt, copy);
        va_end(copy);

        if (written > 0) {
            size_t len = (size_t)written;
            if (len >= sizeof(buffer)) {
                len = sizeof(buffer) - 1;
            }
            m_klog_stream_write(buffer, len);
        }
    }

    if (s_prev_vprintf != NULL) {
        return s_prev_vprintf(fmt, ap);
    }
    return 0;
}

static m_vfs_error_t m_klog_devfs_read(void *private_data,
                                       void *buffer,
                                       size_t size,
                                       size_t *read)
{
    (void)private_data;
    return devfs_stream_try_read(&s_klog_stream, buffer, size, read);
}

static m_vfs_error_t m_klog_devfs_write(void *private_data,
                                        const void *buffer,
                                        size_t size,
                                        size_t *written)
{
    (void)private_data;
    if (buffer == NULL || written == NULL) {
        return M_VFS_ERR_INVALID_PARAM;
    }
    m_klog_stream_write(buffer, size);
    *written = size;
    return M_VFS_ERR_OK;
}

static uint32_t m_klog_devfs_poll(void *private_data)
{
    (void)private_data;
    return devfs_stream_poll(&s_klog_stream);
}

static m_vfs_error_t m_klog_devfs_get_info(void *private_data,
                                           devfs_device_info_t *info)
{
    (void)private_data;
    if (info == NULL) {
        return M_VFS_ERR_INVALID_PARAM;
    }

    memset(info, 0, sizeof(*info));
    strncpy(info->path, s_klog_path, sizeof(info->path));
    info->path[sizeof(info->path) - 1] = '\0';
    strncpy(info->name, "kmsg", sizeof(info->name));
    info->name[sizeof(info->name) - 1] = '\0';
    info->ready_mask = devfs_stream_ready_mask(&s_klog_stream);
    devfs_shm_buffer_info_t buffer_info = {0};
    if (devfs_stream_buffer_info(&s_klog_stream, &buffer_info) == M_VFS_ERR_OK) {
        info->shm_used = buffer_info.used;
        info->shm_capacity = buffer_info.capacity;
    }
    info->tty_echo = false;
    info->tty_canonical = false;
    return M_VFS_ERR_OK;
}

static void m_klog_devfs_attach(const devfs_entry_t *entry,
                                devfs_device_node_t *record)
{
    (void)entry;
    if (record == NULL) {
        return;
    }
    devfs_stream_attach_node(&s_klog_stream, record->node);
}

static void m_klog_devfs_detach(const devfs_entry_t *entry,
                                devfs_device_node_t *record)
{
    (void)entry;
    (void)record;
    devfs_stream_detach_node(&s_klog_stream);
}

static const devfs_ops_t s_klog_ops = {
    .read = m_klog_devfs_read,
    .write = m_klog_devfs_write,
    .poll = m_klog_devfs_poll,
    .get_info = m_klog_devfs_get_info,
};

void m_klog_init(void)
{
    if (s_klog_ready) {
        return;
    }

    if (!devfs_stream_context_init(&s_klog_stream,
                                   s_klog_path,
                                   CONFIG_MAGNOLIA_DEVFS_KMSG_BUFFER_SIZE,
                                   IPC_SHM_RING_OVERWRITE_DROP_OLDEST)) {
        return;
    }

    s_klog_ready = true;
    s_prev_vprintf = esp_log_set_vprintf(m_klog_vprintf);
}

void m_klog_devfs_register(void)
{
    if (!s_klog_ready) {
        m_klog_init();
    }

    if (!s_klog_ready) {
        return;
    }

    (void)devfs_register_ext(s_klog_path,
                             &s_klog_ops,
                             NULL,
                             m_klog_devfs_attach,
                             m_klog_devfs_detach);
}

#else

void m_klog_init(void)
{
}

void m_klog_devfs_register(void)
{
}

#endif
