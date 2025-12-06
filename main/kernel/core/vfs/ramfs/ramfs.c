#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"

#include "sdkconfig.h"
#include "kernel/core/memory/m_alloc.h"
#include "m_vfs.h"
#include "m_vfs_types.h"
#include "core/m_vfs_errno.h"
#include "core/m_vfs_object.h"
#include "core/m_vfs_registry.h"
#include "ramfs.h"

#if CONFIG_MAGNOLIA_RAMFS_ENABLED

typedef struct ramfs_node_data {
    struct ramfs_node_data *parent;
    struct ramfs_node_data *children;
    struct ramfs_node_data *next;
    m_vfs_node_t *vnode;
    char name[M_VFS_NAME_MAX_LEN];
    m_vfs_node_type_t type;
    uint32_t mode;
    size_t size;
    size_t capacity;
    uint8_t *data;
} ramfs_node_data_t;

static portMUX_TYPE g_ramfs_lock =
        (portMUX_TYPE)portMUX_INITIALIZER_UNLOCKED;
static size_t g_ramfs_node_count;

static ramfs_node_data_t *
_ramfs_allocate_node(const char *name,
                      m_vfs_node_type_t type,
                      uint32_t mode)
{
    portENTER_CRITICAL(&g_ramfs_lock);
    if (g_ramfs_node_count >= CONFIG_MAGNOLIA_RAMFS_MAX_NODES) {
        portEXIT_CRITICAL(&g_ramfs_lock);
        return NULL;
    }
    ++g_ramfs_node_count;
    portEXIT_CRITICAL(&g_ramfs_lock);

    ramfs_node_data_t *data = malloc(sizeof(*data));
    if (data == NULL) {
        portENTER_CRITICAL(&g_ramfs_lock);
        --g_ramfs_node_count;
        portEXIT_CRITICAL(&g_ramfs_lock);
        return NULL;
    }

    memset(data, 0, sizeof(*data));
    data->type = type;
    data->mode = mode;
    data->data = NULL;
    data->size = 0;
    data->capacity = 0;
    if (name != NULL) {
        strncpy(data->name, name, M_VFS_NAME_MAX_LEN);
        data->name[M_VFS_NAME_MAX_LEN - 1] = '\0';
    }
    return data;
}

static void
_ramfs_free_node(ramfs_node_data_t *data)
{
    if (data == NULL) {
        return;
    }

    if (data->data != NULL) {
        free(data->data);
    }

    portENTER_CRITICAL(&g_ramfs_lock);
    if (g_ramfs_node_count > 0) {
        --g_ramfs_node_count;
    }
    portEXIT_CRITICAL(&g_ramfs_lock);
    free(data);
}

static ramfs_node_data_t *
_ramfs_node_from_vnode(m_vfs_node_t *node)
{
    if (node == NULL) {
        return NULL;
    }

    return (ramfs_node_data_t *)node->fs_private;
}

static m_vfs_node_t *
_ramfs_node_create(m_vfs_mount_t *mount,
                   const char *name,
                   m_vfs_node_type_t type)
{
    m_vfs_node_t *node = m_vfs_node_create(mount, type);
    if (node == NULL) {
        return NULL;
    }

    ramfs_node_data_t *data = _ramfs_allocate_node(name, type, 0);
    if (data == NULL) {
        m_vfs_node_release(node);
        return NULL;
    }

    node->fs_private = data;
    data->vnode = node;
    return node;
}

static ramfs_node_data_t *
_ramfs_find_child(ramfs_node_data_t *parent, const char *name)
{
    if (parent == NULL || name == NULL) {
        return NULL;
    }

    ramfs_node_data_t *iter = parent->children;
    while (iter != NULL) {
        if (strncmp(iter->name, name, M_VFS_NAME_MAX_LEN) == 0) {
            return iter;
        }
        iter = iter->next;
    }
    return NULL;
}

static void
_ramfs_add_child(ramfs_node_data_t *parent, ramfs_node_data_t *child)
{
    if (parent == NULL || child == NULL) {
        return;
    }

    child->next = parent->children;
    parent->children = child;
    child->parent = parent;
}

static void
_ramfs_remove_child(ramfs_node_data_t *parent, ramfs_node_data_t *child)
{
    if (parent == NULL || child == NULL) {
        return;
    }

    ramfs_node_data_t **slot = &parent->children;
    while (*slot != NULL) {
        if (*slot == child) {
            *slot = child->next;
            child->next = NULL;
            child->parent = NULL;
            return;
        }
        slot = &(*slot)->next;
    }
}

static void
_ramfs_node_destroy(m_vfs_node_t *node)
{
    ramfs_node_data_t *data = _ramfs_node_from_vnode(node);
    if (data == NULL) {
        vPortFree(node);
        return;
    }

    while (data->children != NULL) {
        ramfs_node_data_t *child = data->children;
        _ramfs_remove_child(data, child);
        m_vfs_node_release(child->vnode);
    }

    _ramfs_free_node(data);
    vPortFree(node);
}

static m_vfs_error_t
_ramfs_mount(m_vfs_mount_t *mount, const char *source, void *options)
{
    (void)source;
    (void)options;

    m_vfs_node_t *root = _ramfs_node_create(mount, "/", M_VFS_NODE_TYPE_DIRECTORY);
    if (root == NULL) {
        return M_VFS_ERR_NO_MEMORY;
    }

    mount->root = root;
    return M_VFS_ERR_OK;
}

static m_vfs_error_t
_ramfs_unmount(m_vfs_mount_t *mount)
{
    if (mount == NULL) {
        return M_VFS_ERR_INVALID_PARAM;
    }

    if (mount->root != NULL) {
        m_vfs_node_release(mount->root);
        mount->root = NULL;
    }
    return M_VFS_ERR_OK;
}

static m_vfs_errno_t
_ramfs_lookup_errno(m_vfs_mount_t *mount,
                    m_vfs_node_t *parent,
                    const char *name,
                    m_vfs_node_t **out_node)
{
    if (mount == NULL || parent == NULL || name == NULL || out_node == NULL) {
        return m_vfs_errno_from_vfs_error(M_VFS_ERR_INVALID_PARAM);
    }

    ramfs_node_data_t *parent_data = _ramfs_node_from_vnode(parent);
    if (parent_data == NULL || parent_data->type != M_VFS_NODE_TYPE_DIRECTORY) {
        return m_vfs_errno_from_vfs_error(M_VFS_ERR_INVALID_PARAM);
    }

    ramfs_node_data_t *child = _ramfs_find_child(parent_data, name);
    if (child == NULL) {
        return m_vfs_errno_from_vfs_error(M_VFS_ERR_NOT_FOUND);
    }

    *out_node = child->vnode;
    m_vfs_node_acquire(*out_node);
    return m_vfs_errno_from_vfs_error(M_VFS_ERR_OK);
}

static m_vfs_error_t
_ramfs_lookup(m_vfs_mount_t *mount,
              m_vfs_node_t *parent,
              const char *name,
              m_vfs_node_t **out_node)
{
    return m_vfs_from_errno(_ramfs_lookup_errno(mount,
                                                parent,
                                                name,
                                                out_node));
}

static m_vfs_error_t
_ramfs_create_node(m_vfs_mount_t *mount,
                   m_vfs_node_t *parent,
                   const char *name,
                   m_vfs_node_type_t type,
                   uint32_t mode,
                   m_vfs_node_t **out_node)
{
    if (parent == NULL || name == NULL) {
        return M_VFS_ERR_INVALID_PARAM;
    }

    ramfs_node_data_t *parent_data = _ramfs_node_from_vnode(parent);
    if (parent_data == NULL || parent_data->type != M_VFS_NODE_TYPE_DIRECTORY) {
        return M_VFS_ERR_INVALID_PARAM;
    }

    if (_ramfs_find_child(parent_data, name) != NULL) {
        return M_VFS_ERR_BUSY;
    }

    m_vfs_node_t *node = _ramfs_node_create(mount, name, type);
    if (node == NULL) {
        return M_VFS_ERR_NO_MEMORY;
    }

    ramfs_node_data_t *data = _ramfs_node_from_vnode(node);
    if (data == NULL) {
        m_vfs_node_release(node);
        return M_VFS_ERR_NO_MEMORY;
    }

    data->mode = mode;
    _ramfs_add_child(parent_data, data);

    *out_node = node;
    return M_VFS_ERR_OK;
}

static m_vfs_error_t
_ramfs_create(m_vfs_mount_t *mount,
              m_vfs_node_t *parent,
              const char *name,
              uint32_t mode,
              m_vfs_node_t **out_node)
{
    return _ramfs_create_node(mount, parent, name, M_VFS_NODE_TYPE_FILE, mode, out_node);
}

static m_vfs_error_t
_ramfs_mkdir(m_vfs_mount_t *mount,
             m_vfs_node_t *parent,
             const char *name,
             uint32_t mode,
             m_vfs_node_t **out_node)
{
    return _ramfs_create_node(mount, parent, name, M_VFS_NODE_TYPE_DIRECTORY, mode, out_node);
}

static m_vfs_error_t
_ramfs_unlink(m_vfs_mount_t *mount,
              m_vfs_node_t *parent,
              const char *name)
{
    if (parent == NULL || name == NULL) {
        return M_VFS_ERR_INVALID_PARAM;
    }

    ramfs_node_data_t *parent_data = _ramfs_node_from_vnode(parent);
    if (parent_data == NULL || parent_data->type != M_VFS_NODE_TYPE_DIRECTORY) {
        return M_VFS_ERR_INVALID_PARAM;
    }

    ramfs_node_data_t *child = _ramfs_find_child(parent_data, name);
    if (child == NULL) {
        return M_VFS_ERR_NOT_FOUND;
    }

    if (child->type == M_VFS_NODE_TYPE_DIRECTORY && child->children != NULL) {
        return M_VFS_ERR_BUSY;
    }

    _ramfs_remove_child(parent_data, child);
    m_vfs_node_release(child->vnode);
    return M_VFS_ERR_OK;
}

static m_vfs_error_t
_ramfs_open(m_vfs_node_t *node,
            int flags,
            m_vfs_file_t **out_file)
{
    (void)flags;
    if (node == NULL || out_file == NULL) {
        return M_VFS_ERR_INVALID_PARAM;
    }

    if (node->type != M_VFS_NODE_TYPE_FILE) {
        return M_VFS_ERR_INVALID_PARAM;
    }

    m_vfs_file_t *file = m_vfs_file_create(node);
    if (file == NULL) {
        return M_VFS_ERR_NO_MEMORY;
    }

    *out_file = file;
    return M_VFS_ERR_OK;
}

static m_vfs_error_t
_ramfs_read(m_vfs_file_t *file,
            void *buffer,
            size_t size,
            size_t *read)
{
    if (file == NULL || buffer == NULL || read == NULL) {
        return M_VFS_ERR_INVALID_PARAM;
    }

    ramfs_node_data_t *data = _ramfs_node_from_vnode(file->node);
    if (data == NULL) {
        return M_VFS_ERR_NOT_FOUND;
    }

    size_t available = data->size;
    size_t offset = file->offset;
    if (offset >= available) {
        *read = 0;
        return M_VFS_ERR_OK;
    }

    size_t to_copy = available - offset;
    if (to_copy > size) {
        to_copy = size;
    }

    memcpy(buffer, data->data + offset, to_copy);
    *read = to_copy;
    return M_VFS_ERR_OK;
}

static m_vfs_error_t
_ramfs_write(m_vfs_file_t *file,
             const void *buffer,
             size_t size,
             size_t *written)
{
    if (file == NULL || buffer == NULL || written == NULL) {
        return M_VFS_ERR_INVALID_PARAM;
    }

    ramfs_node_data_t *data = _ramfs_node_from_vnode(file->node);
    if (data == NULL) {
        return M_VFS_ERR_NOT_FOUND;
    }

    size_t target = file->offset + size;
    if (target > data->capacity) {
        size_t new_capacity = target + 64;
        uint8_t *new_buffer = realloc(data->data, new_capacity);
        if (new_buffer == NULL) {
            return M_VFS_ERR_NO_MEMORY;
        }
        data->data = new_buffer;
        data->capacity = new_capacity;
    }

    memcpy(data->data + file->offset, buffer, size);
    data->size = (file->offset + size > data->size) ? file->offset + size : data->size;
    *written = size;
    return M_VFS_ERR_OK;
}

static m_vfs_error_t
_ramfs_readdir(m_vfs_file_t *dir,
               m_vfs_dirent_t *entries,
               size_t capacity,
               size_t *populated)
{
    if (dir == NULL || entries == NULL || populated == NULL) {
        return M_VFS_ERR_INVALID_PARAM;
    }

    ramfs_node_data_t *parent = _ramfs_node_from_vnode(dir->node);
    if (parent == NULL || parent->type != M_VFS_NODE_TYPE_DIRECTORY) {
        return M_VFS_ERR_INVALID_PARAM;
    }

    size_t idx = 0;
    ramfs_node_data_t *child = parent->children;
    while (child != NULL && idx < capacity) {
        strncpy(entries[idx].name, child->name, M_VFS_NAME_MAX_LEN);
        entries[idx].name[M_VFS_NAME_MAX_LEN - 1] = '\0';
        entries[idx].type = child->type;
        entries[idx].node = child->vnode;
        ++idx;
        child = child->next;
    }

    *populated = idx;
    return M_VFS_ERR_OK;
}

static m_vfs_error_t
_ramfs_getattr(m_vfs_node_t *node,
               m_vfs_stat_t *stat)
{
    if (node == NULL || stat == NULL) {
        return M_VFS_ERR_INVALID_PARAM;
    }

    ramfs_node_data_t *data = _ramfs_node_from_vnode(node);
    if (data == NULL) {
        return M_VFS_ERR_NOT_FOUND;
    }

    stat->type = data->type;
    stat->mode = data->mode;
    stat->size = data->size;
    stat->mtime = 0;
    stat->atime = 0;
    stat->flags = 0;
    return M_VFS_ERR_OK;
}

static m_vfs_error_t
_ramfs_setattr(m_vfs_node_t *node,
               const m_vfs_stat_t *stat)
{
    if (node == NULL || stat == NULL) {
        return M_VFS_ERR_INVALID_PARAM;
    }

    ramfs_node_data_t *data = _ramfs_node_from_vnode(node);
    if (data == NULL) {
        return M_VFS_ERR_NOT_FOUND;
    }

    data->mode = stat->mode;
    if (stat->size < data->size) {
        data->size = stat->size;
    }
    return M_VFS_ERR_OK;
}

static const struct m_vfs_fs_ops g_ramfs_ops = {
    .mount = _ramfs_mount,
    .unmount = _ramfs_unmount,
    .lookup = _ramfs_lookup,
    .lookup_errno = _ramfs_lookup_errno,
    .create = _ramfs_create,
    .mkdir = _ramfs_mkdir,
    .unlink = _ramfs_unlink,
    .rmdir = _ramfs_unlink,
    .open = _ramfs_open,
    .close = NULL,
    .read = _ramfs_read,
    .write = _ramfs_write,
    .readdir = _ramfs_readdir,
    .ioctl = NULL,
    .getattr = _ramfs_getattr,
    .setattr = _ramfs_setattr,
    .node_destroy = _ramfs_node_destroy,
    .file_destroy = NULL,
};

static const m_vfs_fs_type_t g_ramfs_type = {
    .name = "ramfs",
    .ops = &g_ramfs_ops,
    .cookie = NULL,
};

const m_vfs_fs_type_t *
m_ramfs_fs_type(void)
{
    return &g_ramfs_type;
}

void
ramfs_diag_tree_snapshot(void (*callback)(const m_vfs_node_t *, void *),
                          void *user_data)
{
    if (callback == NULL) {
        return;
    }

    m_vfs_mount_t *root_mount = m_vfs_registry_mount_find("/");
    if (root_mount == NULL || root_mount->root == NULL) {
        return;
    }

    callback(root_mount->root, user_data);
}

#else

const m_vfs_fs_type_t *
m_ramfs_fs_type(void)
{
    return NULL;
}

void
ramfs_diag_tree_snapshot(void (*callback)(const m_vfs_node_t *, void *),
                          void *user_data)
{
    (void)callback;
    (void)user_data;
}

#endif
