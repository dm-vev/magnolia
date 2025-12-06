#include <stdbool.h>
#include <stddef.h>
#include <stdatomic.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"
#include "freertos/task.h"

#include "sdkconfig.h"
#include "kernel/core/vfs/core/m_vfs_registry.h"

#if CONFIG_MAGNOLIA_VFS_MOUNT_OVERLAYS
#define M_VFS_ENABLE_MOUNT_OVERLAYS 1
#else
#define M_VFS_ENABLE_MOUNT_OVERLAYS 0
#endif

typedef struct m_vfs_fs_type_entry {
    const m_vfs_fs_type_t *type;
    struct m_vfs_fs_type_entry *next;
} m_vfs_fs_type_entry_t;

static portMUX_TYPE g_vfs_fs_types_lock =
        (portMUX_TYPE)portMUX_INITIALIZER_UNLOCKED;
static m_vfs_fs_type_entry_t *g_vfs_fs_types;
static size_t g_vfs_fs_type_count;

static atomic_bool g_vfs_mount_writer = ATOMIC_VAR_INIT(false);
static atomic_size_t g_vfs_mount_readers = ATOMIC_VAR_INIT(0);
static atomic_size_t g_vfs_mount_count = ATOMIC_VAR_INIT(0);
static uint32_t g_vfs_mount_sequence;
static m_vfs_mount_t *g_vfs_mount_slots[CONFIG_MAGNOLIA_VFS_MAX_MOUNTS];

static void
_m_vfs_mount_read_lock(void)
{
    while (true) {
        while (atomic_load_explicit(&g_vfs_mount_writer, memory_order_acquire)) {
            taskYIELD();
        }
        atomic_fetch_add_explicit(&g_vfs_mount_readers, 1, memory_order_acquire);
        if (!atomic_load_explicit(&g_vfs_mount_writer, memory_order_acquire)) {
            break;
        }
        atomic_fetch_sub_explicit(&g_vfs_mount_readers, 1, memory_order_release);
    }
}

static void
_m_vfs_mount_read_unlock(void)
{
    atomic_fetch_sub_explicit(&g_vfs_mount_readers, 1, memory_order_release);
}

static void
_m_vfs_mount_write_lock(void)
{
    while (atomic_exchange_explicit(&g_vfs_mount_writer,
                                    true,
                                    memory_order_acq_rel)) {
        taskYIELD();
    }
    while (atomic_load_explicit(&g_vfs_mount_readers, memory_order_acquire) != 0) {
        taskYIELD();
    }
}

static void
_m_vfs_mount_write_unlock(void)
{
    atomic_store_explicit(&g_vfs_mount_writer, false, memory_order_release);
}

void
m_vfs_registry_init(void)
{
    portENTER_CRITICAL(&g_vfs_fs_types_lock);
    g_vfs_fs_types = NULL;
    g_vfs_fs_type_count = 0;
    portEXIT_CRITICAL(&g_vfs_fs_types_lock);

    atomic_store_explicit(&g_vfs_mount_writer,
                          false,
                          memory_order_relaxed);
    atomic_store_explicit(&g_vfs_mount_readers,
                          0,
                          memory_order_relaxed);

    _m_vfs_mount_write_lock();
    for (size_t i = 0; i < CONFIG_MAGNOLIA_VFS_MAX_MOUNTS; ++i) {
        g_vfs_mount_slots[i] = NULL;
    }
    atomic_store_explicit(&g_vfs_mount_count, 0, memory_order_relaxed);
    g_vfs_mount_sequence = 0;
    _m_vfs_mount_write_unlock();
}

m_vfs_error_t
m_vfs_registry_fs_type_register(const m_vfs_fs_type_t *type)
{
    if (type == NULL || type->name == NULL || type->ops == NULL) {
        return M_VFS_ERR_INVALID_PARAM;
    }

    portENTER_CRITICAL(&g_vfs_fs_types_lock);
    if (g_vfs_fs_type_count >= CONFIG_MAGNOLIA_VFS_MAX_FS_TYPES) {
        portEXIT_CRITICAL(&g_vfs_fs_types_lock);
        return M_VFS_ERR_TOO_MANY_ENTRIES;
    }

    m_vfs_fs_type_entry_t *iter = g_vfs_fs_types;
    while (iter != NULL) {
        if (strcmp(iter->type->name, type->name) == 0) {
            portEXIT_CRITICAL(&g_vfs_fs_types_lock);
            return M_VFS_ERR_BUSY;
        }
        iter = iter->next;
    }

    m_vfs_fs_type_entry_t *entry = pvPortMalloc(sizeof(*entry));
    if (entry == NULL) {
        portEXIT_CRITICAL(&g_vfs_fs_types_lock);
        return M_VFS_ERR_NO_MEMORY;
    }

    entry->type = type;
    entry->next = g_vfs_fs_types;
    g_vfs_fs_types = entry;
    ++g_vfs_fs_type_count;
    portEXIT_CRITICAL(&g_vfs_fs_types_lock);
    return M_VFS_ERR_OK;
}

static bool
_m_vfs_fs_type_used(const m_vfs_fs_type_t *type)
{
    if (type == NULL) {
        return false;
    }

    _m_vfs_mount_read_lock();
    bool used = false;
    for (size_t i = 0; i < CONFIG_MAGNOLIA_VFS_MAX_MOUNTS; ++i) {
        m_vfs_mount_t *mount = g_vfs_mount_slots[i];
        if (mount != NULL && mount->fs_type == type) {
            used = true;
            break;
        }
    }
    _m_vfs_mount_read_unlock();
    return used;
}

m_vfs_error_t
m_vfs_registry_fs_type_unregister(const char *name)
{
    if (name == NULL) {
        return M_VFS_ERR_INVALID_PARAM;
    }

    portENTER_CRITICAL(&g_vfs_fs_types_lock);
    m_vfs_fs_type_entry_t *prev = NULL;
    m_vfs_fs_type_entry_t *iter = g_vfs_fs_types;
    while (iter != NULL) {
        if (strcmp(iter->type->name, name) == 0) {
            if (_m_vfs_fs_type_used(iter->type)) {
                portEXIT_CRITICAL(&g_vfs_fs_types_lock);
                return M_VFS_ERR_BUSY;
            }

            if (prev == NULL) {
                g_vfs_fs_types = iter->next;
            } else {
                prev->next = iter->next;
            }
            vPortFree(iter);
            --g_vfs_fs_type_count;
            portEXIT_CRITICAL(&g_vfs_fs_types_lock);
            return M_VFS_ERR_OK;
        }
        prev = iter;
        iter = iter->next;
    }

    portEXIT_CRITICAL(&g_vfs_fs_types_lock);
    return M_VFS_ERR_NOT_FOUND;
}

const m_vfs_fs_type_t *
m_vfs_registry_fs_type_find(const char *name)
{
    if (name == NULL) {
        return NULL;
    }

    portENTER_CRITICAL(&g_vfs_fs_types_lock);
    m_vfs_fs_type_entry_t *iter = g_vfs_fs_types;
    while (iter != NULL) {
        if (strcmp(iter->type->name, name) == 0) {
            const m_vfs_fs_type_t *result = iter->type;
            portEXIT_CRITICAL(&g_vfs_fs_types_lock);
            return result;
        }
        iter = iter->next;
    }
    portEXIT_CRITICAL(&g_vfs_fs_types_lock);
    return NULL;
}

m_vfs_error_t
m_vfs_registry_mount_add(m_vfs_mount_t *mount)
{
    if (mount == NULL) {
        return M_VFS_ERR_INVALID_PARAM;
    }

    _m_vfs_mount_write_lock();
    if (atomic_load_explicit(&g_vfs_mount_count, memory_order_relaxed) >=
            CONFIG_MAGNOLIA_VFS_MAX_MOUNTS) {
        _m_vfs_mount_write_unlock();
        return M_VFS_ERR_TOO_MANY_ENTRIES;
    }

#if !M_VFS_ENABLE_MOUNT_OVERLAYS
    for (size_t i = 0; i < CONFIG_MAGNOLIA_VFS_MAX_MOUNTS; ++i) {
        if (g_vfs_mount_slots[i] != NULL &&
                strcmp(g_vfs_mount_slots[i]->target, mount->target) == 0) {
            _m_vfs_mount_write_unlock();
            return M_VFS_ERR_BUSY;
        }
    }
#endif

    size_t slot = CONFIG_MAGNOLIA_VFS_MAX_MOUNTS;
    for (size_t i = 0; i < CONFIG_MAGNOLIA_VFS_MAX_MOUNTS; ++i) {
        if (g_vfs_mount_slots[i] == NULL) {
            slot = i;
            break;
        }
    }

    if (slot == CONFIG_MAGNOLIA_VFS_MAX_MOUNTS) {
        _m_vfs_mount_write_unlock();
        return M_VFS_ERR_TOO_MANY_ENTRIES;
    }

    mount->registry_index = slot;
    mount->sequence = ++g_vfs_mount_sequence;
    g_vfs_mount_slots[slot] = mount;
    atomic_fetch_add_explicit(&g_vfs_mount_count, 1, memory_order_relaxed);
    _m_vfs_mount_write_unlock();
    return M_VFS_ERR_OK;
}

m_vfs_mount_t *
m_vfs_registry_mount_find(const char *target)
{
    if (target == NULL) {
        return NULL;
    }

    _m_vfs_mount_read_lock();
    m_vfs_mount_t *result = NULL;
    for (size_t i = 0; i < CONFIG_MAGNOLIA_VFS_MAX_MOUNTS; ++i) {
        m_vfs_mount_t *slot = g_vfs_mount_slots[i];
        if (slot != NULL && strcmp(slot->target, target) == 0) {
            result = slot;
            break;
        }
    }
    _m_vfs_mount_read_unlock();
    return result;
}

void
m_vfs_registry_mount_remove(m_vfs_mount_t *mount)
{
    if (mount == NULL) {
        return;
    }

    _m_vfs_mount_write_lock();
    size_t slot = mount->registry_index;
    if (slot < CONFIG_MAGNOLIA_VFS_MAX_MOUNTS &&
            g_vfs_mount_slots[slot] == mount) {
        g_vfs_mount_slots[slot] = NULL;
        atomic_fetch_sub_explicit(&g_vfs_mount_count, 1, memory_order_relaxed);
        mount->registry_index = SIZE_MAX;
    }
    _m_vfs_mount_write_unlock();
}

static bool
_m_vfs_mount_matches_path(const m_vfs_mount_t *mount,
                          const m_vfs_path_t *path)
{
    if (mount == NULL || path == NULL || mount->target_len == 0) {
        return false;
    }

    if (strncmp(path->normalized, mount->target, mount->target_len) != 0) {
        return false;
    }

    char next = path->normalized[mount->target_len];
    return next == '\0' || next == '/';
}

m_vfs_mount_t *
m_vfs_registry_mount_best(const m_vfs_path_t *path,
                             size_t *match_offset)
{
    if (path == NULL || path->normalized[0] == '\0') {
        if (match_offset != NULL) {
            *match_offset = 0;
        }
        return NULL;
    }

    _m_vfs_mount_read_lock();
    m_vfs_mount_t *best = NULL;
    size_t best_len = 0;
    for (size_t i = 0; i < CONFIG_MAGNOLIA_VFS_MAX_MOUNTS; ++i) {
        m_vfs_mount_t *slot = g_vfs_mount_slots[i];
        if (slot == NULL) {
            continue;
        }
        if (slot->target_len > best_len &&
                _m_vfs_mount_matches_path(slot, path)) {
            best = slot;
            best_len = slot->target_len;
        }
    }
    _m_vfs_mount_read_unlock();

    if (match_offset != NULL) {
        *match_offset = best_len;
    }
    return best;
}

size_t
m_vfs_registry_fs_type_count(void)
{
    portENTER_CRITICAL(&g_vfs_fs_types_lock);
    size_t result = g_vfs_fs_type_count;
    portEXIT_CRITICAL(&g_vfs_fs_types_lock);
    return result;
}

size_t
m_vfs_registry_mount_count(void)
{
    return atomic_load_explicit(&g_vfs_mount_count, memory_order_relaxed);
}

void
m_vfs_registry_iterate_fs_types(bool (*cb)(const m_vfs_fs_type_t *,
                                            void *),
                                 void *user_data)
{
    if (cb == NULL) {
        return;
    }

    portENTER_CRITICAL(&g_vfs_fs_types_lock);
    m_vfs_fs_type_entry_t *iter = g_vfs_fs_types;
    while (iter != NULL) {
        if (!cb(iter->type, user_data)) {
            break;
        }
        iter = iter->next;
    }
    portEXIT_CRITICAL(&g_vfs_fs_types_lock);
}

void
m_vfs_registry_iterate_mounts(bool (*cb)(m_vfs_mount_t *, void *),
                              void *user_data)
{
    if (cb == NULL) {
        return;
    }

    m_vfs_mount_t *snapshot[CONFIG_MAGNOLIA_VFS_MAX_MOUNTS];
    size_t count = 0;

    _m_vfs_mount_read_lock();
    for (size_t i = 0; i < CONFIG_MAGNOLIA_VFS_MAX_MOUNTS; ++i) {
        if (g_vfs_mount_slots[i] != NULL) {
            snapshot[count++] = g_vfs_mount_slots[i];
        }
    }
    _m_vfs_mount_read_unlock();

    for (size_t i = 0; i < count; ++i) {
        if (!cb(snapshot[i], user_data)) {
            break;
        }
    }
}
