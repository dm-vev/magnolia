#include "kernel/core/vfs/cache/m_vfs_read_cache.h"
#include "kernel/core/vfs/core/m_vfs_errno.h"

#include <stdbool.h>
#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "kernel/core/vfs/ramfs/ramfs.h"

#define M_VFS_READ_CACHE_BLOCK_SIZE 512

#if CONFIG_MAGNOLIA_VFS_READ_CACHE

#define M_VFS_READ_CACHE_ENTRY_COUNT CONFIG_MAGNOLIA_VFS_READ_CACHE_SIZE

typedef struct {
    m_vfs_file_t *file;
    size_t block_index;
    size_t valid;
    size_t consumed;
    uint64_t lru;
    bool filled;
    uint8_t data[M_VFS_READ_CACHE_BLOCK_SIZE];
} m_vfs_read_cache_entry_t;

static m_vfs_read_cache_entry_t g_vfs_read_cache_entries[M_VFS_READ_CACHE_ENTRY_COUNT];
static atomic_size_t g_vfs_read_cache_hits;
static atomic_size_t g_vfs_read_cache_misses;
static atomic_size_t g_vfs_read_cache_fills;
static atomic_size_t g_vfs_read_cache_evictions;
static atomic_size_t g_vfs_read_cache_tick;

static inline size_t
_m_vfs_read_cache_next_tick(void)
{
    return atomic_fetch_add_explicit(&g_vfs_read_cache_tick,
                                     1,
                                     memory_order_relaxed) + 1;
}

static inline bool
_m_vfs_read_cache_is_ramfs(const m_vfs_node_t *node)
{
#if CONFIG_MAGNOLIA_RAMFS_ENABLED
    return node != NULL && node->fs_type == m_ramfs_fs_type();
#else
    (void)node;
    return false;
#endif
}

static m_vfs_read_cache_entry_t *
_m_vfs_read_cache_find(const m_vfs_file_t *file, size_t block_index)
{
    if (file == NULL) {
        return NULL;
    }
    for (size_t i = 0; i < M_VFS_READ_CACHE_ENTRY_COUNT; ++i) {
        m_vfs_read_cache_entry_t *entry = &g_vfs_read_cache_entries[i];
        if (entry->filled && entry->file == file &&
                entry->block_index == block_index &&
                entry->consumed < entry->valid) {
            return entry;
        }
    }
    return NULL;
}

static m_vfs_read_cache_entry_t *
_m_vfs_read_cache_reserve(size_t block_index)
{
    m_vfs_read_cache_entry_t *candidate = NULL;
    for (size_t i = 0; i < M_VFS_READ_CACHE_ENTRY_COUNT; ++i) {
        m_vfs_read_cache_entry_t *entry = &g_vfs_read_cache_entries[i];
        if (!entry->filled) {
            candidate = entry;
            break;
        }
        if (candidate == NULL || entry->lru < candidate->lru) {
            candidate = entry;
        }
    }
    if (candidate == NULL) {
        return NULL;
    }
    if (candidate->filled) {
        atomic_fetch_add(&g_vfs_read_cache_evictions, 1);
    }
    candidate->file = NULL;
    candidate->valid = 0;
    candidate->consumed = 0;
    candidate->filled = false;
    candidate->block_index = block_index;
    candidate->lru = 0;
    return candidate;
}

static m_vfs_error_t
_m_vfs_read_cache_fetch_block(m_vfs_file_t *file,
                              size_t block_index,
                              m_vfs_error_t (*driver_read)(m_vfs_file_t *,
                                                           void *,
                                                           size_t,
                                                           size_t *),
                              m_vfs_read_cache_entry_t **out_entry)
{
    if (file == NULL || driver_read == NULL || out_entry == NULL) {
        return M_VFS_ERR_INVALID_PARAM;
    }

    m_vfs_read_cache_entry_t *entry = _m_vfs_read_cache_reserve(block_index);
    if (entry == NULL) {
        return M_VFS_ERR_NO_MEMORY;
    }

    size_t block_bytes = 0;
    m_vfs_error_t err = m_vfs_record_error(driver_read(file,
                                                       entry->data,
                                                       M_VFS_READ_CACHE_BLOCK_SIZE,
                                                       &block_bytes));
    if (err != M_VFS_ERR_OK) {
        entry->filled = false;
        entry->file = NULL;
        entry->valid = 0;
        entry->consumed = 0;
        return err;
    }

    if (block_bytes == 0) {
        entry->filled = false;
        entry->file = NULL;
        entry->valid = 0;
        entry->consumed = 0;
        return M_VFS_ERR_OK;
    }

    entry->file = file;
    entry->block_index = block_index;
    entry->valid = block_bytes;
    entry->consumed = 0;
    entry->filled = true;
    entry->lru = _m_vfs_read_cache_next_tick();
    atomic_fetch_add(&g_vfs_read_cache_fills, 1);
    atomic_fetch_add(&g_vfs_read_cache_misses, 1);
    *out_entry = entry;
    return M_VFS_ERR_OK;
}

static inline void
_m_vfs_read_cache_copy(m_vfs_read_cache_entry_t *entry,
                       void *buffer,
                       size_t size,
                       size_t *copied)
{
    size_t available = entry->valid - entry->consumed;
    size_t take = (size < available) ? size : available;
    memcpy(buffer, entry->data + entry->consumed, take);
    entry->consumed += take;
    entry->lru = _m_vfs_read_cache_next_tick();
    if (entry->consumed == entry->valid) {
        entry->filled = false;
        entry->file = NULL;
        entry->valid = 0;
        entry->consumed = 0;
    }
    atomic_fetch_add(&g_vfs_read_cache_hits, take);
    *copied = take;
}

bool
m_vfs_read_cache_enabled(void)
{
    return true;
}

bool
m_vfs_read_cache_enabled_for(const m_vfs_file_t *file)
{
    if (file == NULL || file->node == NULL || file->node->fs_type == NULL) {
        return false;
    }
    return !_m_vfs_read_cache_is_ramfs(file->node);
}

m_vfs_error_t
m_vfs_read_cache_read(m_vfs_file_t *file,
                      void *buffer,
                      size_t size,
                      size_t *read,
                      m_vfs_error_t (*driver_read)(m_vfs_file_t *,
                                                   void *,
                                                   size_t,
                                                   size_t *))
{
    if (read == NULL || buffer == NULL) {
        return M_VFS_ERR_INVALID_PARAM;
    }
    *read = 0;

    if (!m_vfs_read_cache_enabled_for(file) || driver_read == NULL || size == 0) {
        return M_VFS_ERR_NOT_SUPPORTED;
    }

    size_t total = 0;
    while (total < size) {
        size_t offset = file->offset + total;
        size_t block_index = offset / M_VFS_READ_CACHE_BLOCK_SIZE;
        m_vfs_read_cache_entry_t *entry = _m_vfs_read_cache_find(file, block_index);
        if (entry == NULL) {
            m_vfs_error_t err = _m_vfs_read_cache_fetch_block(file,
                                                               block_index,
                                                               driver_read,
                                                               &entry);
            if (err != M_VFS_ERR_OK) {
                return err;
            }
            if (entry == NULL || entry->valid == 0) {
                break;
            }
        }

        size_t chunk = 0;
        _m_vfs_read_cache_copy(entry,
                               (uint8_t *)buffer + total,
                               size - total,
                               &chunk);
        if (chunk == 0) {
            break;
        }
        total += chunk;
    }

    *read = total;
    return M_VFS_ERR_OK;
}

void
m_vfs_read_cache_flush_file(const m_vfs_file_t *file)
{
    if (file == NULL) {
        return;
    }
    for (size_t i = 0; i < M_VFS_READ_CACHE_ENTRY_COUNT; ++i) {
        m_vfs_read_cache_entry_t *entry = &g_vfs_read_cache_entries[i];
        if (entry->file == file) {
            entry->file = NULL;
            entry->filled = false;
            entry->valid = 0;
            entry->consumed = 0;
        }
    }
}

void
m_vfs_read_cache_flush_all(void)
{
    for (size_t i = 0; i < M_VFS_READ_CACHE_ENTRY_COUNT; ++i) {
        m_vfs_read_cache_entry_t *entry = &g_vfs_read_cache_entries[i];
        entry->file = NULL;
        entry->filled = false;
        entry->valid = 0;
        entry->consumed = 0;
    }
    atomic_store(&g_vfs_read_cache_hits, 0);
    atomic_store(&g_vfs_read_cache_misses, 0);
    atomic_store(&g_vfs_read_cache_fills, 0);
    atomic_store(&g_vfs_read_cache_evictions, 0);
    atomic_store(&g_vfs_read_cache_tick, 0);
}

void
m_vfs_read_cache_stats(m_vfs_read_cache_stats_t *stats)
{
    if (stats == NULL) {
        return;
    }
    stats->hits = atomic_load(&g_vfs_read_cache_hits);
    stats->misses = atomic_load(&g_vfs_read_cache_misses);
    stats->fills = atomic_load(&g_vfs_read_cache_fills);
    stats->evictions = atomic_load(&g_vfs_read_cache_evictions);
    stats->entries = M_VFS_READ_CACHE_ENTRY_COUNT;
    stats->block_size = M_VFS_READ_CACHE_BLOCK_SIZE;
}

#else

bool
m_vfs_read_cache_enabled(void)
{
    return false;
}

bool
m_vfs_read_cache_enabled_for(const m_vfs_file_t *file)
{
    (void)file;
    return false;
}

m_vfs_error_t
m_vfs_read_cache_read(m_vfs_file_t *file,
                      void *buffer,
                      size_t size,
                      size_t *read,
                      m_vfs_error_t (*driver_read)(m_vfs_file_t *,
                                                   void *,
                                                   size_t,
                                                   size_t *))
{
    (void)file;
    (void)buffer;
    (void)size;
    (void)driver_read;
    if (read != NULL) {
        *read = 0;
    }
    return M_VFS_ERR_NOT_SUPPORTED;
}

void
m_vfs_read_cache_flush_file(const m_vfs_file_t *file)
{
    (void)file;
}

void
m_vfs_read_cache_flush_all(void)
{
}

void
m_vfs_read_cache_stats(m_vfs_read_cache_stats_t *stats)
{
    if (stats == NULL) {
        return;
    }
    stats->hits = 0;
    stats->misses = 0;
    stats->fills = 0;
    stats->evictions = 0;
    stats->entries = 0;
    stats->block_size = 0;
}

#endif /* CONFIG_MAGNOLIA_VFS_READ_CACHE */
