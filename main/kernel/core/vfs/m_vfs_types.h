/**
 * @file        kernel/core/vfs/m_vfs_types.h
 * @brief       Core Magnolia VFS type declarations.
 * @details     Defines the unified node, file, mount, and filesystem driver
 *              descriptors used by the VFS core and drivers.
 */
#ifndef MAGNOLIA_VFS_M_VFS_TYPES_H
#define MAGNOLIA_VFS_M_VFS_TYPES_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdatomic.h>

#include "kernel/core/vfs/core/m_vfs_errno.h"

#include "freertos/portmacro.h"
#include "sdkconfig.h"
#include "kernel/core/ipc/ipc_scheduler_bridge.h"

#define M_VFS_FS_NAME_MAX_LEN 32
#define M_VFS_PATH_MAX_LEN CONFIG_MAGNOLIA_VFS_MAX_PATH_LEN
#define M_VFS_PATH_SEGMENT_MAX 32
#define M_VFS_NAME_MAX_LEN 64
#define M_VFS_FILE_MODE_DEFAULT 0644
#define M_VFS_DIRECTORY_MODE_DEFAULT 0755

typedef enum m_vfs_error {
    M_VFS_ERR_OK = 0,
    M_VFS_ERR_INVALID_PARAM,
    M_VFS_ERR_NOT_FOUND,
    M_VFS_ERR_NOT_SUPPORTED,
    M_VFS_ERR_NO_MEMORY,
    M_VFS_ERR_TOO_MANY_ENTRIES,
    M_VFS_ERR_INVALID_PATH,
    M_VFS_ERR_BUSY,
    M_VFS_ERR_INTERRUPTED,
    M_VFS_ERR_IO,
    M_VFS_ERR_WOULD_BLOCK,
    M_VFS_ERR_TIMEOUT,
    M_VFS_ERR_DESTROYED,
} m_vfs_error_t;

typedef enum {
    M_VFS_NODE_TYPE_UNKNOWN = 0,
    M_VFS_NODE_TYPE_DIRECTORY,
    M_VFS_NODE_TYPE_FILE,
    M_VFS_NODE_TYPE_DEVICE,
    M_VFS_NODE_TYPE_SYMLINK,
} m_vfs_node_type_t;

struct m_vfs_mount;
struct m_vfs_node;
struct m_vfs_file;
struct m_vfs_path;

typedef struct {
    const char *name;
    size_t length;
} m_vfs_path_segment_t;

typedef struct {
    char normalized[M_VFS_PATH_MAX_LEN];
    size_t segment_count;
    m_vfs_path_segment_t segments[M_VFS_PATH_SEGMENT_MAX];
} m_vfs_path_t;

typedef struct {
    struct m_vfs_node *node;
    char name[M_VFS_NAME_MAX_LEN];
    m_vfs_node_type_t type;
} m_vfs_dirent_t;

typedef struct {
    m_vfs_node_type_t type;
    uint32_t mode;
    size_t size;
    uint64_t mtime;
    uint64_t atime;
    uint32_t flags;
} m_vfs_stat_t;

struct m_vfs_fs_ops {
    m_vfs_error_t (*mount)(struct m_vfs_mount *mount,
                           const char *source,
                           void *options);
    m_vfs_error_t (*unmount)(struct m_vfs_mount *mount);
    m_vfs_error_t (*lookup)(struct m_vfs_mount *mount,
                            struct m_vfs_node *parent,
                            const char *name,
                            struct m_vfs_node **out_node);
    m_vfs_errno_t (*lookup_errno)(struct m_vfs_mount *mount,
                                  struct m_vfs_node *parent,
                                  const char *name,
                                  struct m_vfs_node **out_node);
    m_vfs_error_t (*create)(struct m_vfs_mount *mount,
                            struct m_vfs_node *parent,
                            const char *name,
                            uint32_t mode,
                            struct m_vfs_node **out_node);
    m_vfs_error_t (*mkdir)(struct m_vfs_mount *mount,
                           struct m_vfs_node *parent,
                           const char *name,
                           uint32_t mode,
                           struct m_vfs_node **out_node);
    m_vfs_error_t (*unlink)(struct m_vfs_mount *mount,
                            struct m_vfs_node *parent,
                            const char *name);
    m_vfs_error_t (*rmdir)(struct m_vfs_mount *mount,
                           struct m_vfs_node *parent,
                           const char *name);
    m_vfs_error_t (*open)(struct m_vfs_node *node,
                          int flags,
                          struct m_vfs_file **out_file);
    m_vfs_error_t (*close)(struct m_vfs_file *file);
    m_vfs_error_t (*read)(struct m_vfs_file *file,
                          void *buffer,
                          size_t size,
                          size_t *read);
    m_vfs_error_t (*write)(struct m_vfs_file *file,
                           const void *buffer,
                           size_t size,
                           size_t *written);
    m_vfs_error_t (*readdir)(struct m_vfs_file *dir,
                             m_vfs_dirent_t *entries,
                             size_t capacity,
                             size_t *populated);
    m_vfs_error_t (*ioctl)(struct m_vfs_file *file,
                           unsigned long request,
                           void *arg);
    m_vfs_error_t (*getattr)(struct m_vfs_node *node,
                             m_vfs_stat_t *stat);
    m_vfs_error_t (*setattr)(struct m_vfs_node *node,
                             const m_vfs_stat_t *stat);
    void (*node_destroy)(struct m_vfs_node *node);
    void (*file_destroy)(struct m_vfs_file *file);
};

typedef struct m_vfs_fs_type {
    const char *name;
    const struct m_vfs_fs_ops *ops;
    void *cookie;
} m_vfs_fs_type_t;

typedef struct m_vfs_node {
    const m_vfs_fs_type_t *fs_type;
    struct m_vfs_mount *mount;
    struct m_vfs_node *parent;
    m_vfs_node_type_t type;
    portMUX_TYPE lock;
    atomic_size_t refcount;
    void *fs_private;
    bool destroyed;
    struct m_vfs_node *list_next;
} m_vfs_node_t;

typedef struct m_vfs_file {
    m_vfs_node_t *node;
    portMUX_TYPE lock;
    atomic_size_t refcount;
    size_t offset;
    void *fs_private;
    bool closed;
    bool destroyed;
    ipc_wait_queue_t waiters;
    portMUX_TYPE wait_lock;
} m_vfs_file_t;

typedef struct m_vfs_mount {
    const m_vfs_fs_type_t *fs_type;
    m_vfs_node_t *root;
    void *fs_private;
    char target[M_VFS_PATH_MAX_LEN];
    portMUX_TYPE lock;
    bool active;
    size_t refcount;
    struct m_vfs_mount *next;
    size_t target_len;
    uint32_t sequence;
    size_t registry_index;
} m_vfs_mount_t;

#endif /* MAGNOLIA_VFS_M_VFS_TYPES_H */
