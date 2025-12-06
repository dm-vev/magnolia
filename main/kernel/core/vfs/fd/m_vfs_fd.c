#include <stdbool.h>
#include <stddef.h>

#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"

#include "sdkconfig.h"
#include "kernel/core/job/m_job_core.h"
#include "kernel/core/job/m_job_event.h"
#include "kernel/core/vfs/core/m_vfs_jobcwd.h"
#include "kernel/core/vfs/core/m_vfs_object.h"
#include "kernel/core/vfs/fd/m_vfs_fd.h"

#if CONFIG_MAGNOLIA_VFS_FD_LOGGING
#include "esp_log.h"
#define TAG "vfs/fd"
#define M_VFS_FD_LOG(fmt, ...) \
    ESP_LOGD(TAG, fmt, ##__VA_ARGS__)
#else
#define M_VFS_FD_LOG(fmt, ...) \
    ((void)0)
#endif

#define M_VFS_JOB_FD_CAPACITY CONFIG_MAGNOLIA_VFS_MAX_OPEN_FILES_PER_JOB
#define M_VFS_KERNEL_FD_CAPACITY CONFIG_MAGNOLIA_VFS_MAX_OPEN_FILES_GLOBAL

typedef struct {
    bool in_use;
    m_vfs_file_t *file;
} m_vfs_fd_entry_t;

typedef struct m_vfs_job_fd_table {
    m_job_id_t owner;
    portMUX_TYPE lock;
    m_vfs_fd_entry_t entries[M_VFS_JOB_FD_CAPACITY];
    struct m_vfs_job_fd_table *next;
} m_vfs_job_fd_table_t;

static portMUX_TYPE g_vfs_job_fd_lock =
        (portMUX_TYPE)portMUX_INITIALIZER_UNLOCKED;
static m_vfs_job_fd_table_t *g_vfs_job_fd_tables;

static m_vfs_fd_entry_t g_vfs_kernel_entries[M_VFS_KERNEL_FD_CAPACITY];
static portMUX_TYPE g_vfs_kernel_lock =
        (portMUX_TYPE)portMUX_INITIALIZER_UNLOCKED;

static m_vfs_job_fd_table_t *_m_vfs_job_fd_table_find(m_job_id_t job,
                                                       bool create)
{
    if (job == NULL) {
        return NULL;
    }

    portENTER_CRITICAL(&g_vfs_job_fd_lock);
    m_vfs_job_fd_table_t *iter = g_vfs_job_fd_tables;
    while (iter != NULL && iter->owner != job) {
        iter = iter->next;
    }

    if (iter == NULL && create) {
        iter = pvPortMalloc(sizeof(*iter));
        if (iter != NULL) {
            iter->owner = job;
            iter->lock = (portMUX_TYPE)portMUX_INITIALIZER_UNLOCKED;
            for (size_t i = 0; i < M_VFS_JOB_FD_CAPACITY; ++i) {
                iter->entries[i].in_use = false;
                iter->entries[i].file = NULL;
            }
            iter->next = g_vfs_job_fd_tables;
            g_vfs_job_fd_tables = iter;
        }
    }

    portEXIT_CRITICAL(&g_vfs_job_fd_lock);
    return iter;
}

static void
_m_vfs_fd_table_cleanup(m_vfs_job_fd_table_t *table)
{
    if (table == NULL) {
        return;
    }

    portENTER_CRITICAL(&table->lock);
    for (size_t i = 0; i < M_VFS_JOB_FD_CAPACITY; ++i) {
        if (table->entries[i].in_use) {
            table->entries[i].in_use = false;
            m_vfs_file_release(table->entries[i].file);
            table->entries[i].file = NULL;
        }
    }
    portEXIT_CRITICAL(&table->lock);
}

static void
_m_vfs_job_destroyed_cb(m_job_id_t job, void *user_data)
{
    (void)user_data;

    if (job == NULL) {
        return;
    }

    portENTER_CRITICAL(&job->lock);
    if (job->ctx != NULL) {
        job->ctx->cwd[0] = '/';
        job->ctx->cwd[1] = '\0';
    }
    portEXIT_CRITICAL(&job->lock);

    m_vfs_job_cwd_remove(job);

    portENTER_CRITICAL(&g_vfs_job_fd_lock);
    m_vfs_job_fd_table_t *prev = NULL;
    m_vfs_job_fd_table_t *iter = g_vfs_job_fd_tables;
    while (iter != NULL) {
        if (iter->owner == job) {
            if (prev == NULL) {
                g_vfs_job_fd_tables = iter->next;
            } else {
                prev->next = iter->next;
            }
            break;
        }
        prev = iter;
        iter = iter->next;
    }
    portEXIT_CRITICAL(&g_vfs_job_fd_lock);

    _m_vfs_fd_table_cleanup(iter);
    if (iter != NULL) {
        vPortFree(iter);
    }
}

void m_vfs_fd_init(void)
{
    portENTER_CRITICAL(&g_vfs_kernel_lock);
    for (size_t i = 0; i < M_VFS_KERNEL_FD_CAPACITY; ++i) {
        g_vfs_kernel_entries[i].in_use = false;
        g_vfs_kernel_entries[i].file = NULL;
    }
    portEXIT_CRITICAL(&g_vfs_kernel_lock);
    m_job_subscribe_destroy(_m_vfs_job_destroyed_cb, NULL);
}

static m_vfs_fd_entry_t *
_m_vfs_entries_for_job(m_job_id_t job,
                       size_t *capacity,
                       portMUX_TYPE **lock)
{
    if (job == NULL) {
        if (capacity != NULL) {
            *capacity = M_VFS_KERNEL_FD_CAPACITY;
        }
        if (lock != NULL) {
            *lock = &g_vfs_kernel_lock;
        }
        return g_vfs_kernel_entries;
    }

    m_vfs_job_fd_table_t *table = _m_vfs_job_fd_table_find(job, true);
    if (table == NULL) {
        return NULL;
    }

    if (capacity != NULL) {
        *capacity = M_VFS_JOB_FD_CAPACITY;
    }
    if (lock != NULL) {
        *lock = &table->lock;
    }
    return table->entries;
}

int m_vfs_fd_allocate(m_job_id_t job, m_vfs_file_t *file)
{
    if (file == NULL) {
        return -1;
    }

    size_t capacity = 0;
    portMUX_TYPE *lock = NULL;
    m_vfs_fd_entry_t *entries = _m_vfs_entries_for_job(job, &capacity, &lock);
    if (entries == NULL || lock == NULL) {
        return -1;
    }

    portENTER_CRITICAL(lock);
    int slot = -1;
    for (size_t i = 0; i < capacity; ++i) {
        if (!entries[i].in_use) {
            entries[i].in_use = true;
            entries[i].file = file;
            m_vfs_file_acquire(file);
            slot = (int)i;
            break;
        }
    }
    portEXIT_CRITICAL(lock);
    if (slot >= 0) {
        M_VFS_FD_LOG("allocated fd=%d job=%p file=%p",
                     slot,
                     (void *)job,
                     (void *)file);
    }
    return slot;
}

m_vfs_file_t *m_vfs_fd_lookup(m_job_id_t job, int fd)
{
    size_t capacity = 0;
    portMUX_TYPE *lock = NULL;
    m_vfs_fd_entry_t *entries = _m_vfs_entries_for_job(job, &capacity, &lock);
    if (entries == NULL || lock == NULL || fd < 0 || (size_t)fd >= capacity) {
        return NULL;
    }

    m_vfs_file_t *result = NULL;
    portENTER_CRITICAL(lock);
    if (entries[fd].in_use) {
        result = entries[fd].file;
    }
    portEXIT_CRITICAL(lock);
    return result;
}

void m_vfs_fd_release(m_job_id_t job, int fd)
{
    size_t capacity = 0;
    portMUX_TYPE *lock = NULL;
    m_vfs_fd_entry_t *entries = _m_vfs_entries_for_job(job, &capacity, &lock);
    if (entries == NULL || lock == NULL || fd < 0 || (size_t)fd >= capacity) {
        return;
    }

    portENTER_CRITICAL(lock);
    if (entries[fd].in_use) {
        entries[fd].in_use = false;
        m_vfs_file_release(entries[fd].file);
        entries[fd].file = NULL;
        M_VFS_FD_LOG("released fd=%d job=%p",
                     fd,
                     (void *)job);
    }
    portEXIT_CRITICAL(lock);
}

size_t m_vfs_fd_kernel_capacity(void)
{
    return M_VFS_KERNEL_FD_CAPACITY;
}

size_t m_vfs_fd_job_table_count(void)
{
    size_t count = 0;
    portENTER_CRITICAL(&g_vfs_job_fd_lock);
    m_vfs_job_fd_table_t *iter = g_vfs_job_fd_tables;
    while (iter != NULL) {
        ++count;
        iter = iter->next;
    }
    portEXIT_CRITICAL(&g_vfs_job_fd_lock);
    return count;
}

void m_vfs_fd_foreach(m_vfs_fd_diag_iter_fn cb, void *user_data)
{
    if (cb == NULL) {
        return;
    }

    portENTER_CRITICAL(&g_vfs_job_fd_lock);
    m_vfs_job_fd_table_t *iter = g_vfs_job_fd_tables;
    while (iter != NULL) {
        for (size_t i = 0; i < M_VFS_JOB_FD_CAPACITY; ++i) {
            if (!iter->entries[i].in_use) {
                continue;
            }
            if (!cb(iter->owner, (int)i, iter->entries[i].file, user_data)) {
                portEXIT_CRITICAL(&g_vfs_job_fd_lock);
                return;
            }
        }
        iter = iter->next;
    }
    portEXIT_CRITICAL(&g_vfs_job_fd_lock);

    portENTER_CRITICAL(&g_vfs_kernel_lock);
    for (size_t i = 0; i < M_VFS_KERNEL_FD_CAPACITY; ++i) {
        if (!g_vfs_kernel_entries[i].in_use) {
            continue;
        }
        if (!cb(NULL, (int)i, g_vfs_kernel_entries[i].file, user_data)) {
            portEXIT_CRITICAL(&g_vfs_kernel_lock);
            return;
        }
    }
    portEXIT_CRITICAL(&g_vfs_kernel_lock);
}

size_t m_vfs_fd_job_table_snapshot(m_vfs_fd_job_table_snapshot_t *buffer,
                                   size_t capacity)
{
    if (buffer == NULL || capacity == 0) {
        return 0;
    }

    size_t count = 0;
    portENTER_CRITICAL(&g_vfs_job_fd_lock);
    m_vfs_job_fd_table_t *iter = g_vfs_job_fd_tables;
    while (iter != NULL && count < capacity) {
        size_t used = 0;
        for (size_t i = 0; i < M_VFS_JOB_FD_CAPACITY; ++i) {
            if (iter->entries[i].in_use) {
                ++used;
            }
        }
        buffer[count].job = iter->owner;
        buffer[count].used = used;
        ++count;
        iter = iter->next;
    }
    portEXIT_CRITICAL(&g_vfs_job_fd_lock);
    return count;
}

/**
 * @brief Assign a pre-existing file descriptor slot to a file.
 */
m_vfs_error_t
m_vfs_fd_assign(m_job_id_t job,
                int fd,
                m_vfs_file_t *file)
{
    if (file == NULL) {
        return M_VFS_ERR_INVALID_PARAM;
    }

    size_t capacity = 0;
    portMUX_TYPE *lock = NULL;
    m_vfs_fd_entry_t *entries = _m_vfs_entries_for_job(job, &capacity, &lock);
    if (entries == NULL || lock == NULL || fd < 0 || (size_t)fd >= capacity) {
        return M_VFS_ERR_INVALID_PARAM;
    }

    portENTER_CRITICAL(lock);
    if (entries[fd].in_use) {
        m_vfs_file_release(entries[fd].file);
    }
    entries[fd].in_use = true;
    entries[fd].file = file;
    m_vfs_file_acquire(file);
    portEXIT_CRITICAL(lock);

    M_VFS_FD_LOG("assigned fd=%d job=%p file=%p",
                 fd,
                 (void *)job,
                 (void *)file);
    return M_VFS_ERR_OK;
}

static bool
_m_vfs_fd_entry_matches_mount(const m_vfs_file_t *file,
                              const m_vfs_mount_t *mount)
{
    if (file == NULL || mount == NULL || file->node == NULL) {
        return false;
    }

    return file->node->mount == mount;
}

void
m_vfs_fd_close_mount_fds(m_vfs_mount_t *mount)
{
    if (mount == NULL) {
        return;
    }

    portENTER_CRITICAL(&g_vfs_job_fd_lock);
    m_vfs_job_fd_table_t *iter = g_vfs_job_fd_tables;
    while (iter != NULL) {
        portENTER_CRITICAL(&iter->lock);
        for (size_t i = 0; i < M_VFS_JOB_FD_CAPACITY; ++i) {
            if (!iter->entries[i].in_use ||
                    !_m_vfs_fd_entry_matches_mount(iter->entries[i].file, mount)) {
                continue;
            }
            iter->entries[i].in_use = false;
            m_vfs_file_release(iter->entries[i].file);
            iter->entries[i].file = NULL;
        }
        portEXIT_CRITICAL(&iter->lock);
        iter = iter->next;
    }
    portEXIT_CRITICAL(&g_vfs_job_fd_lock);

    portENTER_CRITICAL(&g_vfs_kernel_lock);
    for (size_t i = 0; i < M_VFS_KERNEL_FD_CAPACITY; ++i) {
        if (!g_vfs_kernel_entries[i].in_use ||
                !_m_vfs_fd_entry_matches_mount(g_vfs_kernel_entries[i].file, mount)) {
            continue;
        }
        g_vfs_kernel_entries[i].in_use = false;
        m_vfs_file_release(g_vfs_kernel_entries[i].file);
        g_vfs_kernel_entries[i].file = NULL;
    }
    portEXIT_CRITICAL(&g_vfs_kernel_lock);
}
