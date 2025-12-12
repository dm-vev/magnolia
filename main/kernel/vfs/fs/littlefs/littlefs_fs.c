#include <fcntl.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <inttypes.h>

#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"
#include "freertos/semphr.h"

#include "sdkconfig.h"

#include "esp_log.h"
#include "esp_partition.h"

#include "kernel/core/vfs/core/m_vfs_errno.h"
#include "kernel/core/vfs/core/m_vfs_object.h"
#include "kernel/core/vfs/m_vfs_types.h"
#include "kernel/core/vfs/m_vfs.h"
#include "kernel/vfs/fs/littlefs/lfs_backend_flash.h"
#include "kernel/vfs/fs/littlefs/littlefs_fs.h"
#include "kernel/vfs/fs/littlefs/lfs.h"

#if !CONFIG_MAGNOLIA_LITTLEFS_ENABLED
const m_vfs_fs_type_t *
m_littlefs_fs_type(void)
{
    return NULL;
}

#else

typedef struct {
    lfs_t lfs;
    struct lfs_config cfg;
    SemaphoreHandle_t lock;
    littlefs_flash_ctx_t *flash;
} littlefs_mount_data_t;

typedef struct {
    char path[M_VFS_PATH_MAX_LEN];
    bool is_dir;
} littlefs_node_data_t;

typedef struct {
    lfs_file_t file;
    littlefs_mount_data_t *mount;
    bool is_dir;
} littlefs_file_data_t;

static const struct m_vfs_fs_ops s_littlefs_ops;
static const m_vfs_fs_type_t s_littlefs_type = {
    .name = "littlefs",
    .ops = &s_littlefs_ops,
    .cookie = NULL,
};

const m_vfs_fs_type_t *
m_littlefs_fs_type(void)
{
    return &s_littlefs_type;
}

static const char *
littlefs_path_for_lfs(const char *path)
{
    if (path == NULL || path[0] == '\0') {
        return ".";
    }
    return path;
}

static void
littlefs_log_partitions(void)
{
    esp_partition_iterator_t it = esp_partition_find(ESP_PARTITION_TYPE_ANY,
                                                     ESP_PARTITION_SUBTYPE_ANY,
                                                     NULL);
    if (it == NULL) {
        ESP_LOGW("littlefs", "no partitions found");
        return;
    }

    ESP_LOGI("littlefs", "available partitions:");
    while (it != NULL) {
        const esp_partition_t *p = esp_partition_get(it);
        if (p != NULL) {
            ESP_LOGI("littlefs",
                     "label=%s type=0x%02x subtype=0x%02x addr=0x%08"PRIx32" size=%"PRIu32" erase=%"PRIu32,
                     p->label, p->type, p->subtype, p->address, p->size, p->erase_size);
        }
        it = esp_partition_next(it);
    }
    esp_partition_iterator_release(it);
}

static bool
littlefs_lock_take(littlefs_mount_data_t *data)
{
    if (data == NULL || data->lock == NULL) {
        return false;
    }
    TickType_t ticks = pdMS_TO_TICKS(CONFIG_MAGNOLIA_LITTLEFS_LOCK_TIMEOUT_MS);
    if (ticks == 0) {
        ticks = 1;
    }
    return xSemaphoreTake(data->lock, ticks) == pdTRUE;
}

static void
littlefs_lock_give(littlefs_mount_data_t *data)
{
    if (data == NULL || data->lock == NULL) {
        return;
    }
    xSemaphoreGive(data->lock);
}

static bool
littlefs_path_join(const char *parent,
                   const char *name,
                   char *out,
                   size_t capacity)
{
    if (out == NULL || name == NULL) {
        return false;
    }

    if (parent == NULL || parent[0] == '\0') {
        if (snprintf(out, capacity, "%s", name) < 0 ||
                (size_t)strlen(name) >= capacity) {
            return false;
        }
        return true;
    }

    int written = snprintf(out, capacity, "%s/%s", parent, name);
    return (written >= 0 && (size_t)written < capacity);
}

static littlefs_node_data_t *
littlefs_node_data_create(const char *path, bool is_dir)
{
    littlefs_node_data_t *node = pvPortMalloc(sizeof(*node));
    if (node == NULL) {
        return NULL;
    }

    if (path != NULL) {
        strncpy(node->path, path, sizeof(node->path));
    } else {
        node->path[0] = '\0';
    }
    node->path[sizeof(node->path) - 1] = '\0';
    node->is_dir = is_dir;
    return node;
}

static void
littlefs_node_destroy(m_vfs_node_t *node)
{
    if (node == NULL || node->fs_private == NULL) {
        return;
    }
    vPortFree(node->fs_private);
    node->fs_private = NULL;
}

static void
littlefs_file_destroy(m_vfs_file_t *file)
{
    if (file == NULL || file->fs_private == NULL) {
        return;
    }

    littlefs_file_data_t *data = file->fs_private;
    if (data->mount != NULL) {
        if (!data->is_dir) {
            if (littlefs_lock_take(data->mount)) {
                lfs_file_close(&data->mount->lfs, &data->file);
                littlefs_lock_give(data->mount);
            }
        }
    }
    vPortFree(data);
    file->fs_private = NULL;
}

static m_vfs_error_t
littlefs_error_translate(int err)
{
    if (err >= 0) {
        return M_VFS_ERR_OK;
    }

    switch (err) {
    case LFS_ERR_NOENT:
        return M_VFS_ERR_NOT_FOUND;
    case LFS_ERR_EXIST:
        return M_VFS_ERR_BUSY;
    case LFS_ERR_NOTEMPTY:
        return M_VFS_ERR_BUSY;
    case LFS_ERR_ISDIR:
    case LFS_ERR_NOTDIR:
    case LFS_ERR_NAMETOOLONG:
        return M_VFS_ERR_INVALID_PARAM;
    case LFS_ERR_CORRUPT:
    case LFS_ERR_INVAL:
        return M_VFS_ERR_INVALID_PARAM;
    case LFS_ERR_NOSPC:
    case LFS_ERR_NOMEM:
        return M_VFS_ERR_NO_MEMORY;
    case LFS_ERR_IO:
        return M_VFS_ERR_INTERRUPTED;
    default:
        return M_VFS_ERR_INVALID_PARAM;
    }
}

static littlefs_mount_data_t *
littlefs_mount_data(m_vfs_mount_t *mount)
{
    if (mount == NULL) {
        return NULL;
    }
    return (littlefs_mount_data_t *)mount->fs_private;
}

static m_vfs_error_t
littlefs_lookup_node(m_vfs_mount_t *mount,
                     const littlefs_node_data_t *parent,
                     const char *name,
                     m_vfs_node_t **out_node)
{
    if (mount == NULL || parent == NULL || name == NULL || out_node == NULL) {
        return M_VFS_ERR_INVALID_PARAM;
    }

    char child_path[M_VFS_PATH_MAX_LEN];
    if (!littlefs_path_join(parent->path, name, child_path, sizeof(child_path))) {
        return M_VFS_ERR_INVALID_PATH;
    }

    littlefs_mount_data_t *data = littlefs_mount_data(mount);
    if (data == NULL) {
        return M_VFS_ERR_INVALID_PARAM;
    }

    struct lfs_info info;
    if (!littlefs_lock_take(data)) {
        return M_VFS_ERR_TIMEOUT;
    }
    int err = lfs_stat(&data->lfs, littlefs_path_for_lfs(child_path), &info);
    littlefs_lock_give(data);
    if (err < 0) {
        return littlefs_error_translate(err);
    }

    m_vfs_node_type_t type =
            (info.type == LFS_TYPE_DIR) ? M_VFS_NODE_TYPE_DIRECTORY : M_VFS_NODE_TYPE_FILE;
    m_vfs_node_t *node = m_vfs_node_create(mount, type);
    if (node == NULL) {
        return M_VFS_ERR_NO_MEMORY;
    }

    littlefs_node_data_t *node_data = littlefs_node_data_create(child_path,
                                                 info.type == LFS_TYPE_DIR);
    if (node_data == NULL) {
        m_vfs_node_release(node);
        return M_VFS_ERR_NO_MEMORY;
    }
    node->fs_private = node_data;
    *out_node = node;
    return M_VFS_ERR_OK;
}

static m_vfs_error_t
littlefs_mount(m_vfs_mount_t *mount,
               const char *source,
               void *options)
{
    (void)source;

    littlefs_mount_options_t *mount_opts = (littlefs_mount_options_t *)options;
    littlefs_mount_data_t *data = pvPortMalloc(sizeof(*data));
    if (data == NULL) {
        return M_VFS_ERR_NO_MEMORY;
    }

    memset(data, 0, sizeof(*data));
    data->lock = xSemaphoreCreateMutex();
    if (data->lock == NULL) {
        vPortFree(data);
        return M_VFS_ERR_NO_MEMORY;
    }

    const char *label = NULL;
    if (mount_opts != NULL && mount_opts->partition_label != NULL &&
        mount_opts->partition_label[0] != '\0') {
        label = mount_opts->partition_label;
    } else if (CONFIG_MAGNOLIA_LITTLEFS_PARTITION_LABEL[0] != '\0') {
        label = CONFIG_MAGNOLIA_LITTLEFS_PARTITION_LABEL;
    }

    if (label == NULL) {
        vSemaphoreDelete(data->lock);
        vPortFree(data);
        return M_VFS_ERR_INVALID_PARAM;
    }

    const esp_partition_t *part = esp_partition_find_first(
            ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, label);
    if (part == NULL) {
        ESP_LOGE("littlefs", "partition '%s' not found", label);
        littlefs_log_partitions();
        vSemaphoreDelete(data->lock);
        vPortFree(data);
        return M_VFS_ERR_NOT_FOUND;
    }

    littlefs_flash_ctx_t *ctx = pvPortMalloc(sizeof(*ctx));
    if (ctx == NULL) {
        vSemaphoreDelete(data->lock);
        vPortFree(data);
        return M_VFS_ERR_NO_MEMORY;
    }

    uint32_t erase_size = part->erase_size;
    uint32_t block_size = CONFIG_MAGNOLIA_LITTLEFS_BLOCK_SIZE;
    if (erase_size == 0) {
        erase_size = 4096;
    }
    if (block_size < erase_size || (block_size % erase_size) != 0) {
        ESP_LOGW("littlefs", "block_size %u invalid for erase_size %u, using %u",
                 (unsigned)block_size, (unsigned)erase_size, (unsigned)erase_size);
        block_size = erase_size;
    }
    uint32_t max_blocks = part->size / block_size;
    uint32_t cfg_blocks = CONFIG_MAGNOLIA_LITTLEFS_BLOCK_COUNT;
    uint32_t block_count = max_blocks;
    if (cfg_blocks > 0 && cfg_blocks < max_blocks) {
        block_count = cfg_blocks;
    } else if (cfg_blocks > max_blocks) {
        ESP_LOGW("littlefs", "block_count %u exceeds partition, clamping to %u",
                 (unsigned)cfg_blocks, (unsigned)max_blocks);
        block_count = max_blocks;
    }

    ctx->partition = part;
    ctx->base = 0;
    ctx->size = part->size;
    ctx->block_size = block_size;
    ctx->read_only = (mount_opts != NULL && mount_opts->read_only);
    data->flash = ctx;

    ESP_LOGI("littlefs", "mount label=%s offset=0x%08"PRIx32" size=%"PRIu32" block=%"PRIu32" blocks=%"PRIu32" ro=%d",
             label, part->address, part->size, block_size, block_count, ctx->read_only);

    data->cfg.context = ctx;
    data->cfg.read = littlefs_backend_read;
    data->cfg.prog = littlefs_backend_prog;
    data->cfg.erase = littlefs_backend_erase;
    data->cfg.sync = littlefs_backend_sync;
    data->cfg.read_size = CONFIG_MAGNOLIA_LITTLEFS_READ_SIZE;
    data->cfg.prog_size = CONFIG_MAGNOLIA_LITTLEFS_PROG_SIZE;
    data->cfg.block_size = block_size;
    data->cfg.block_count = block_count;
    data->cfg.block_cycles = CONFIG_MAGNOLIA_LITTLEFS_BLOCK_CYCLES;
    data->cfg.cache_size = CONFIG_MAGNOLIA_LITTLEFS_CACHE_SIZE;
    data->cfg.lookahead_size = CONFIG_MAGNOLIA_LITTLEFS_LOOKAHEAD_SIZE;
    data->cfg.compact_thresh = 0;
    data->cfg.read_buffer = NULL;
    data->cfg.prog_buffer = NULL;
    data->cfg.lookahead_buffer = NULL;

    bool force_format = (mount_opts != NULL && mount_opts->force_format);
    bool format_if_fail = (mount_opts != NULL && (mount_opts->format_if_empty ||
                                                 mount_opts->format_if_mount_fails))
            || CONFIG_MAGNOLIA_LITTLEFS_FORMAT_IF_FAIL;

    int err = LFS_ERR_OK;
    if (force_format && !ctx->read_only) {
        err = lfs_format(&data->lfs, &data->cfg);
        if (err < 0) {
            err = littlefs_error_translate(err);
        }
    }

    err = lfs_mount(&data->lfs, &data->cfg);
    if (err < 0) {
        if (format_if_fail && !ctx->read_only) {
            lfs_format(&data->lfs, &data->cfg);
            err = lfs_mount(&data->lfs, &data->cfg);
        }
        if (err < 0) {
            vPortFree(ctx);
            vSemaphoreDelete(data->lock);
            vPortFree(data);
            return littlefs_error_translate(err);
        }
    }

    mount->fs_private = data;
    m_vfs_node_t *root = m_vfs_node_create(mount, M_VFS_NODE_TYPE_DIRECTORY);
    if (root == NULL) {
        lfs_unmount(&data->lfs);
        vPortFree(data);
        return M_VFS_ERR_NO_MEMORY;
    }

    littlefs_node_data_t *root_data = littlefs_node_data_create("", true);
    if (root_data == NULL) {
        m_vfs_node_release(root);
        lfs_unmount(&data->lfs);
        vPortFree(data);
        return M_VFS_ERR_NO_MEMORY;
    }

    root->fs_private = root_data;
    mount->root = root;
    return M_VFS_ERR_OK;
}

static m_vfs_error_t
littlefs_unmount(m_vfs_mount_t *mount)
{
    littlefs_mount_data_t *data = littlefs_mount_data(mount);
    if (data == NULL) {
        return M_VFS_ERR_OK;
    }

    lfs_unmount(&data->lfs);
    if (data->flash != NULL) {
        vPortFree(data->flash);
    }
    if (data->lock != NULL) {
        vSemaphoreDelete(data->lock);
    }
    vPortFree(data);
    mount->fs_private = NULL;

    if (mount->root != NULL) {
        m_vfs_node_release(mount->root);
        mount->root = NULL;
    }
    return M_VFS_ERR_OK;
}

static m_vfs_error_t
littlefs_lookup(m_vfs_mount_t *mount,
                m_vfs_node_t *parent,
                const char *name,
                m_vfs_node_t **out_node)
{
    if (parent == NULL || parent->fs_private == NULL) {
        return M_VFS_ERR_INVALID_PARAM;
    }

    littlefs_node_data_t *parent_data = parent->fs_private;
    return littlefs_lookup_node(mount, parent_data, name, out_node);
}

static m_vfs_error_t
littlefs_create(m_vfs_mount_t *mount,
                m_vfs_node_t *parent,
                const char *name,
                uint32_t mode,
                m_vfs_node_t **out_node)
{
    (void)mode;
    if (parent == NULL || name == NULL) {
        return M_VFS_ERR_INVALID_PARAM;
    }

    littlefs_node_data_t *parent_data = parent->fs_private;
    if (parent_data == NULL || !parent_data->is_dir) {
        return M_VFS_ERR_INVALID_PARAM;
    }

    char child_path[M_VFS_PATH_MAX_LEN];
    if (!littlefs_path_join(parent_data->path, name, child_path, sizeof(child_path))) {
        return M_VFS_ERR_INVALID_PATH;
    }

    littlefs_mount_data_t *data = littlefs_mount_data(mount);
    if (data == NULL) {
        return M_VFS_ERR_INVALID_PARAM;
    }

    if (!littlefs_lock_take(data)) {
        return M_VFS_ERR_TIMEOUT;
    }
    lfs_file_t file;
    int err = lfs_file_open(&data->lfs,
                            &file,
                            littlefs_path_for_lfs(child_path),
                            LFS_O_CREAT | LFS_O_WRONLY);
    if (err >= 0) {
        lfs_file_close(&data->lfs, &file);
    }
    littlefs_lock_give(data);

    if (err < 0) {
        return littlefs_error_translate(err);
    }

    if (out_node != NULL) {
        return littlefs_lookup_node(mount, parent_data, name, out_node);
    }
    return M_VFS_ERR_OK;
}

static m_vfs_error_t
littlefs_mkdir(m_vfs_mount_t *mount,
               m_vfs_node_t *parent,
               const char *name,
               uint32_t mode,
               m_vfs_node_t **out_node)
{
    (void)mode;
    if (parent == NULL || name == NULL) {
        return M_VFS_ERR_INVALID_PARAM;
    }

    littlefs_node_data_t *parent_data = parent->fs_private;
    if (parent_data == NULL || !parent_data->is_dir) {
        return M_VFS_ERR_INVALID_PARAM;
    }

    char child_path[M_VFS_PATH_MAX_LEN];
    if (!littlefs_path_join(parent_data->path, name, child_path, sizeof(child_path))) {
        return M_VFS_ERR_INVALID_PATH;
    }

    littlefs_mount_data_t *data = littlefs_mount_data(mount);
    if (data == NULL) {
        return M_VFS_ERR_INVALID_PARAM;
    }

    if (!littlefs_lock_take(data)) {
        return M_VFS_ERR_TIMEOUT;
    }
    int err = lfs_mkdir(&data->lfs, littlefs_path_for_lfs(child_path));
    littlefs_lock_give(data);
    if (err < 0) {
        return littlefs_error_translate(err);
    }

    if (out_node != NULL) {
        return littlefs_lookup_node(mount, parent_data, name, out_node);
    }
    return M_VFS_ERR_OK;
}

static m_vfs_error_t
littlefs_unlink(m_vfs_mount_t *mount,
                m_vfs_node_t *parent,
                const char *name)
{
    (void)parent;
    if (name == NULL) {
        return M_VFS_ERR_INVALID_PARAM;
    }

    littlefs_node_data_t *parent_data = parent->fs_private;
    if (parent_data == NULL) {
        return M_VFS_ERR_INVALID_PARAM;
    }

    char child_path[M_VFS_PATH_MAX_LEN];
    if (!littlefs_path_join(parent_data->path, name, child_path, sizeof(child_path))) {
        return M_VFS_ERR_INVALID_PATH;
    }

    littlefs_mount_data_t *data = littlefs_mount_data(mount);
    if (data == NULL) {
        return M_VFS_ERR_INVALID_PARAM;
    }

    if (!littlefs_lock_take(data)) {
        return M_VFS_ERR_TIMEOUT;
    }
    int err = lfs_remove(&data->lfs, littlefs_path_for_lfs(child_path));
    littlefs_lock_give(data);
    return littlefs_error_translate(err);
}

static m_vfs_error_t
littlefs_open(m_vfs_node_t *node,
              int flags,
              m_vfs_file_t **out_file)
{
    if (node == NULL || out_file == NULL) {
        return M_VFS_ERR_INVALID_PARAM;
    }

    littlefs_node_data_t *node_data = node->fs_private;
    if (node_data == NULL) {
        return M_VFS_ERR_INVALID_PARAM;
    }

    littlefs_mount_data_t *data = littlefs_mount_data(node->mount);
    if (data == NULL) {
        return M_VFS_ERR_INVALID_PARAM;
    }

    littlefs_file_data_t *file_data = pvPortMalloc(sizeof(*file_data));
    if (file_data == NULL) {
        return M_VFS_ERR_NO_MEMORY;
    }
    file_data->mount = data;
    file_data->is_dir = node_data->is_dir;

    if (node_data->is_dir) {
        if ((flags & (O_WRONLY | O_RDWR)) != 0) {
            vPortFree(file_data);
            return M_VFS_ERR_INVALID_PARAM;
        }
        m_vfs_file_t *file = m_vfs_file_create(node);
        if (file == NULL) {
            vPortFree(file_data);
            return M_VFS_ERR_NO_MEMORY;
        }
        file->fs_private = file_data;
        *out_file = file;
        return M_VFS_ERR_OK;
    }

    int lfs_flags = LFS_O_RDONLY;
    if ((flags & O_RDWR) == O_RDWR) {
        lfs_flags = LFS_O_RDWR;
    } else if (flags & O_WRONLY) {
        lfs_flags = LFS_O_WRONLY;
    }
    if (flags & O_CREAT) {
        lfs_flags |= LFS_O_CREAT;
    }
    if (flags & O_TRUNC) {
        lfs_flags |= LFS_O_TRUNC;
    }
    if (flags & O_APPEND) {
        lfs_flags |= LFS_O_APPEND;
    }

    if (!littlefs_lock_take(data)) {
        vPortFree(file_data);
        return M_VFS_ERR_TIMEOUT;
    }
    int err = lfs_file_open(&data->lfs,
                             &file_data->file,
                             littlefs_path_for_lfs(node_data->path),
                             lfs_flags);
    littlefs_lock_give(data);

    if (err < 0) {
        vPortFree(file_data);
        return littlefs_error_translate(err);
    }

    m_vfs_file_t *file = m_vfs_file_create(node);
    if (file == NULL) {
        if (littlefs_lock_take(data)) {
            lfs_file_close(&data->lfs, &file_data->file);
            littlefs_lock_give(data);
        }
        vPortFree(file_data);
        return M_VFS_ERR_NO_MEMORY;
    }
    file->fs_private = file_data;
    *out_file = file;
    return M_VFS_ERR_OK;
}

static m_vfs_error_t
littlefs_close(m_vfs_file_t *file)
{
    littlefs_file_destroy(file);
    return M_VFS_ERR_OK;
}

static m_vfs_error_t
littlefs_read(m_vfs_file_t *file,
              void *buffer,
              size_t size,
              size_t *read)
{
    if (file == NULL || buffer == NULL || read == NULL) {
        return M_VFS_ERR_INVALID_PARAM;
    }

    littlefs_file_data_t *data = file->fs_private;
    if (data == NULL || data->mount == NULL) {
        return M_VFS_ERR_INVALID_PARAM;
    }
    if (data->is_dir) {
        return M_VFS_ERR_INVALID_PARAM;
    }

    if (!littlefs_lock_take(data->mount)) {
        return M_VFS_ERR_TIMEOUT;
    }
    lfs_ssize_t result = lfs_file_read(&data->mount->lfs,
                                       &data->file,
                                       buffer,
                                       size);
    littlefs_lock_give(data->mount);

    if (result < 0) {
        return littlefs_error_translate((int)result);
    }

    *read = (size_t)result;
    return M_VFS_ERR_OK;
}

static m_vfs_error_t
littlefs_write(m_vfs_file_t *file,
               const void *buffer,
               size_t size,
               size_t *written)
{
    if (file == NULL || buffer == NULL || written == NULL) {
        return M_VFS_ERR_INVALID_PARAM;
    }

    littlefs_file_data_t *data = file->fs_private;
    if (data == NULL || data->mount == NULL) {
        return M_VFS_ERR_INVALID_PARAM;
    }
    if (data->is_dir) {
        return M_VFS_ERR_INVALID_PARAM;
    }

    if (!littlefs_lock_take(data->mount)) {
        return M_VFS_ERR_TIMEOUT;
    }
    lfs_ssize_t result = lfs_file_write(&data->mount->lfs,
                                        &data->file,
                                        buffer,
                                        size);
    if (result >= 0) {
        lfs_file_sync(&data->mount->lfs, &data->file);
    }
    littlefs_lock_give(data->mount);

    if (result < 0) {
        return littlefs_error_translate((int)result);
    }

    *written = (size_t)result;
    return M_VFS_ERR_OK;
}

static m_vfs_error_t
littlefs_readdir(m_vfs_file_t *dir,
                 m_vfs_dirent_t *entries,
                 size_t capacity,
                 size_t *populated)
{
    if (dir == NULL || entries == NULL || populated == NULL) {
        return M_VFS_ERR_INVALID_PARAM;
    }

    littlefs_node_data_t *node_data = dir->node->fs_private;
    if (node_data == NULL || !node_data->is_dir) {
        return M_VFS_ERR_INVALID_PARAM;
    }

    littlefs_mount_data_t *data = littlefs_mount_data(dir->node->mount);
    if (data == NULL) {
        return M_VFS_ERR_INVALID_PARAM;
    }

    lfs_dir_t ldir;
    if (!littlefs_lock_take(data)) {
        return M_VFS_ERR_TIMEOUT;
    }
    int err = lfs_dir_open(&data->lfs,
                           &ldir,
                           littlefs_path_for_lfs(node_data->path));
    if (err < 0) {
        littlefs_lock_give(data);
        return littlefs_error_translate(err);
    }

    size_t count = 0;
    struct lfs_info info;
    while (count < capacity) {
        err = lfs_dir_read(&data->lfs, &ldir, &info);
        if (err <= 0) {
            break;
        }
        strncpy(entries[count].name,
                info.name,
                M_VFS_NAME_MAX_LEN);
        entries[count].name[M_VFS_NAME_MAX_LEN - 1] = '\0';
        entries[count].type = (info.type == LFS_TYPE_DIR)
                ? M_VFS_NODE_TYPE_DIRECTORY
                : M_VFS_NODE_TYPE_FILE;
        entries[count].node = NULL;
        ++count;
    }
    lfs_dir_close(&data->lfs, &ldir);
    littlefs_lock_give(data);

    *populated = count;
    return M_VFS_ERR_OK;
}

static m_vfs_error_t
littlefs_getattr(m_vfs_node_t *node,
                 m_vfs_stat_t *stat)
{
    if (node == NULL || stat == NULL) {
        return M_VFS_ERR_INVALID_PARAM;
    }

    littlefs_node_data_t *node_data = node->fs_private;
    if (node_data == NULL) {
        return M_VFS_ERR_INVALID_PARAM;
    }

    littlefs_mount_data_t *data = littlefs_mount_data(node->mount);
    if (data == NULL) {
        return M_VFS_ERR_INVALID_PARAM;
    }

    struct lfs_info info;
    if (!littlefs_lock_take(data)) {
        return M_VFS_ERR_TIMEOUT;
    }
    int err = lfs_stat(&data->lfs,
                       littlefs_path_for_lfs(node_data->path),
                       &info);
    littlefs_lock_give(data);

    if (err < 0) {
        return littlefs_error_translate(err);
    }

    stat->type = (info.type == LFS_TYPE_DIR)
            ? M_VFS_NODE_TYPE_DIRECTORY
            : M_VFS_NODE_TYPE_FILE;
    stat->size = info.size;
    stat->mode = (info.type == LFS_TYPE_DIR)
            ? M_VFS_DIRECTORY_MODE_DEFAULT
            : M_VFS_FILE_MODE_DEFAULT;
    stat->mtime = 0;
    stat->atime = 0;
    stat->flags = 0;
    return M_VFS_ERR_OK;
}

static m_vfs_error_t
littlefs_setattr(m_vfs_node_t *node,
                 const m_vfs_stat_t *stat)
{
    (void)stat;
    if (node == NULL || stat == NULL) {
        return M_VFS_ERR_INVALID_PARAM;
    }

    if (stat->size == 0) {
        return M_VFS_ERR_OK;
    }

    littlefs_node_data_t *node_data = node->fs_private;
    if (node_data == NULL || node_data->is_dir) {
        return M_VFS_ERR_INVALID_PARAM;
    }

    littlefs_mount_data_t *data = littlefs_mount_data(node->mount);
    if (data == NULL) {
        return M_VFS_ERR_INVALID_PARAM;
    }

    if (!littlefs_lock_take(data)) {
        return M_VFS_ERR_TIMEOUT;
    }
    lfs_file_t file;
    int err = lfs_file_open(&data->lfs,
                            &file,
                            littlefs_path_for_lfs(node_data->path),
                            LFS_O_RDWR);
    if (err < 0) {
        littlefs_lock_give(data);
        return littlefs_error_translate(err);
    }

    err = lfs_file_truncate(&data->lfs, &file, stat->size);
    lfs_file_close(&data->lfs, &file);
    littlefs_lock_give(data);

    return littlefs_error_translate(err);
}

static m_vfs_errno_t
littlefs_lookup_errno(struct m_vfs_mount *mount,
                      struct m_vfs_node *parent,
                      const char *name,
                      struct m_vfs_node **out_node);

static const struct m_vfs_fs_ops s_littlefs_ops = {
    .mount = littlefs_mount,
    .unmount = littlefs_unmount,
    .lookup = littlefs_lookup,
    .lookup_errno = littlefs_lookup_errno,
    .create = littlefs_create,
    .mkdir = littlefs_mkdir,
    .unlink = littlefs_unlink,
    .rmdir = littlefs_unlink,
    .open = littlefs_open,
    .close = littlefs_close,
    .read = littlefs_read,
    .write = littlefs_write,
    .readdir = littlefs_readdir,
    .ioctl = NULL,
    .getattr = littlefs_getattr,
    .setattr = littlefs_setattr,
    .node_destroy = littlefs_node_destroy,
    .file_destroy = littlefs_file_destroy,
};

static m_vfs_errno_t
littlefs_lookup_errno(struct m_vfs_mount *mount,
                      struct m_vfs_node *parent,
                      const char *name,
                      struct m_vfs_node **out_node)
{
    return m_vfs_errno_from_vfs_error(littlefs_lookup(mount,
                                                       parent,
                                                       name,
                                                       out_node));
}

#endif /* CONFIG_MAGNOLIA_LITTLEFS_ENABLED */
