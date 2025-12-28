#include "kernel/vfs/fs/devfs/devfs_alloc.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"

#include "kernel/core/memory/m_alloc.h"
#include "kernel/vfs/fs/devfs/devfs.h"

static const char *const ALLOC_TAG = "devfs_alloc";

typedef struct {
    portMUX_TYPE lock;
    bool read_ready;
    size_t open_count;
    char line[64];
    size_t line_len;
} devfs_alloc_device_t;

static devfs_alloc_device_t g_devfs_alloc = {
    .lock = portMUX_INITIALIZER_UNLOCKED,
};

static bool parse_limit_bytes(const char *buf, size_t len, size_t *out_bytes)
{
    if (buf == NULL || out_bytes == NULL) {
        return false;
    }

    while (len > 0 && isspace((unsigned char)buf[0])) {
        buf++;
        len--;
    }
    while (len > 0 && isspace((unsigned char)buf[len - 1])) {
        len--;
    }
    if (len == 0) {
        return false;
    }

    bool percent = false;
    if (buf[len - 1] == '%') {
        percent = true;
        len--;
        if (len == 0) {
            return false;
        }
    }

    size_t value = 0;
    size_t idx = 0;
    while (idx < len && isdigit((unsigned char)buf[idx])) {
        size_t digit = (size_t)(buf[idx] - '0');
        value = value * 10 + digit;
        idx++;
    }
    if (idx == 0) {
        return false;
    }

    if (percent) {
        if (value > 100) {
            return false;
        }
        size_t total = heap_caps_get_total_size(MALLOC_CAP_DEFAULT);
        *out_bytes = (total * value) / 100U;
        return true;
    }

    size_t mult = 1;
    if (idx < len) {
        char suffix = (char)tolower((unsigned char)buf[idx]);
        if (suffix == 'k') {
            mult = 1024U;
            idx++;
        } else if (suffix == 'm') {
            mult = 1024U * 1024U;
            idx++;
        } else if (suffix == 'g') {
            mult = 1024U * 1024U * 1024U;
            idx++;
        }
    }

    while (idx < len && isspace((unsigned char)buf[idx])) {
        idx++;
    }
    if (idx != len) {
        return false;
    }

    *out_bytes = value * mult;
    return true;
}

static bool line_has_nonspace(const char *buf, size_t len)
{
    for (size_t i = 0; i < len; ++i) {
        if (!isspace((unsigned char)buf[i])) {
            return true;
        }
    }
    return false;
}

static m_vfs_error_t devfs_alloc_apply_line(devfs_alloc_device_t *device,
                                            const char *line,
                                            size_t len)
{
    if (!line_has_nonspace(line, len)) {
        return M_VFS_ERR_OK;
    }

    size_t bytes = 0;
    if (!parse_limit_bytes(line, len, &bytes)) {
        return M_VFS_ERR_INVALID_PARAM;
    }

    if (!m_alloc_set_job_heap_limit_bytes(bytes)) {
        return M_VFS_ERR_INVALID_PARAM;
    }

    portENTER_CRITICAL(&device->lock);
    device->read_ready = true;
    portEXIT_CRITICAL(&device->lock);
    ESP_LOGI(ALLOC_TAG, "job heap limit set to %u bytes", (unsigned)bytes);
    return M_VFS_ERR_OK;
}

static m_vfs_error_t devfs_alloc_read(void *private_data,
                                      void *buffer,
                                      size_t size,
                                      size_t *read)
{
    devfs_alloc_device_t *device = (devfs_alloc_device_t *)private_data;
    if (buffer == NULL || read == NULL) {
        return M_VFS_ERR_INVALID_PARAM;
    }
    if (device == NULL) {
        return M_VFS_ERR_INVALID_PARAM;
    }

    bool ready = false;
    portENTER_CRITICAL(&device->lock);
    ready = device->read_ready;
    device->read_ready = false;
    portEXIT_CRITICAL(&device->lock);

    if (!ready) {
        *read = 0;
        return M_VFS_ERR_OK;
    }

    char out[32];
    size_t limit = m_alloc_get_job_heap_limit_bytes();
    int n = snprintf(out, sizeof(out), "%u\n", (unsigned)limit);
    if (n < 0) {
        return M_VFS_ERR_IO;
    }
    size_t want = (size_t)n;
    if (want > size) {
        want = size;
    }
    memcpy(buffer, out, want);
    *read = want;
    return M_VFS_ERR_OK;
}

static m_vfs_error_t devfs_alloc_write(void *private_data,
                                       const void *buffer,
                                       size_t size,
                                       size_t *written)
{
    devfs_alloc_device_t *device = (devfs_alloc_device_t *)private_data;
    if (buffer == NULL || written == NULL) {
        return M_VFS_ERR_INVALID_PARAM;
    }
    if (device == NULL) {
        return M_VFS_ERR_INVALID_PARAM;
    }

    const char *input = (const char *)buffer;
    size_t consumed = 0;
    while (consumed < size) {
        char ch = input[consumed++];
        if (ch == '\n' || device->line_len + 1 >= sizeof(device->line)) {
            size_t line_len = device->line_len;
            device->line[device->line_len] = '\0';
            device->line_len = 0;
            m_vfs_error_t err = devfs_alloc_apply_line(device, device->line, line_len);
            if (err != M_VFS_ERR_OK) {
                return err;
            }
            if (ch == '\n') {
                continue;
            }
        }
        device->line[device->line_len++] = ch;
    }

    *written = size;
    return M_VFS_ERR_OK;
}

static uint32_t devfs_alloc_poll(void *private_data)
{
    devfs_alloc_device_t *device = (devfs_alloc_device_t *)private_data;
    if (device == NULL) {
        return DEVFS_EVENT_WRITABLE;
    }
    portENTER_CRITICAL(&device->lock);
    bool ready = device->read_ready;
    portEXIT_CRITICAL(&device->lock);
    return (ready ? DEVFS_EVENT_READABLE : 0) | DEVFS_EVENT_WRITABLE;
}

static m_vfs_error_t devfs_alloc_open(void *private_data)
{
    devfs_alloc_device_t *device = (devfs_alloc_device_t *)private_data;
    if (device == NULL) {
        return M_VFS_ERR_INVALID_PARAM;
    }
    portENTER_CRITICAL(&device->lock);
    device->open_count += 1;
    device->read_ready = true;
    portEXIT_CRITICAL(&device->lock);
    return M_VFS_ERR_OK;
}

static m_vfs_error_t devfs_alloc_close(void *private_data)
{
    devfs_alloc_device_t *device = (devfs_alloc_device_t *)private_data;
    if (device == NULL) {
        return M_VFS_ERR_INVALID_PARAM;
    }
    portENTER_CRITICAL(&device->lock);
    if (device->open_count > 0) {
        device->open_count -= 1;
    }
    portEXIT_CRITICAL(&device->lock);
    return M_VFS_ERR_OK;
}

static const devfs_ops_t s_devfs_alloc_ops = {
    .open = devfs_alloc_open,
    .close = devfs_alloc_close,
    .read = devfs_alloc_read,
    .write = devfs_alloc_write,
    .poll = devfs_alloc_poll,
};

bool devfs_alloc_register(void)
{
    m_vfs_error_t err = devfs_register("/dev/alloc", &s_devfs_alloc_ops, &g_devfs_alloc);
    if (err != M_VFS_ERR_OK) {
        ESP_LOGE(ALLOC_TAG, "register /dev/alloc failed err=%d", err);
        return false;
    }
    ESP_LOGI(ALLOC_TAG, "registered /dev/alloc");
    return true;
}
