#ifndef MAGNOLIA_VFS_DEVFS_INTERNAL_H
#define MAGNOLIA_VFS_DEVFS_INTERNAL_H

#include "kernel/vfs/fs/devfs/devfs.h"
#include "kernel/core/vfs/m_vfs_types.h"
#include "kernel/core/vfs/fd/m_vfs_fd.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct devfs_device_node devfs_device_node_t;
typedef struct devfs_mount_data devfs_mount_data_t;

typedef struct devfs_entry {
    char path[M_VFS_PATH_MAX_LEN];
    char name[M_VFS_NAME_MAX_LEN];
    const devfs_ops_t *ops;
    void *private_data;
    bool registered;
    size_t node_count;
    void (*node_attach)(const devfs_entry_t *, devfs_device_node_t *);
    void (*node_detach)(const devfs_entry_t *, devfs_device_node_t *);
    devfs_device_node_t *nodes;
    struct devfs_entry *next;
} devfs_entry_t;

typedef struct devfs_node_data {
    devfs_entry_t *entry;
    devfs_device_node_t *device;
    char name[M_VFS_NAME_MAX_LEN];
    char path[M_VFS_PATH_MAX_LEN];
    bool is_directory;
} devfs_node_data_t;

typedef struct devfs_device_node {
    m_vfs_node_t *node;
    const devfs_entry_t *entry;
    devfs_mount_data_t *mount;
    bool is_directory;
    portMUX_TYPE lock;
    devfs_event_mask_t ready_mask;
    size_t notify_count;
    size_t poll_count;
    size_t blocked_count;
    struct devfs_device_node *next_mount;
    struct devfs_device_node *next_entry;
} devfs_device_node_t;

typedef struct devfs_mount_data {
    m_vfs_mount_t *mount;
    m_vfs_node_t *root;
    devfs_device_node_t *nodes;
    portMUX_TYPE lock;
    bool pending_free;
    struct devfs_mount_data *next;
} devfs_mount_data_t;

const devfs_entry_t *devfs_entry_from_node(const m_vfs_node_t *node);

#ifdef __cplusplus
}
#endif

#endif /* MAGNOLIA_VFS_DEVFS_INTERNAL_H */
