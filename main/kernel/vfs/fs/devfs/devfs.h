#ifndef MAGNOLIA_VFS_DEVFS_H
#define MAGNOLIA_VFS_DEVFS_H

#include <stddef.h>
#include <stdbool.h>

#include "sdkconfig.h"
#include "kernel/core/vfs/m_vfs_types.h"

struct devfs_entry;
typedef struct devfs_entry devfs_entry_t;
struct devfs_device_node;
typedef struct devfs_device_node devfs_device_node_t;

#ifdef __cplusplus
extern "C" {
#endif

typedef uint32_t devfs_event_mask_t;

#define DEVFS_EVENT_READABLE (1u << 0)
#define DEVFS_EVENT_WRITABLE (1u << 1)
#define DEVFS_EVENT_ERROR    (1u << 2)
#define DEVFS_EVENT_HANGUP   (1u << 3)

typedef struct {
    char path[M_VFS_PATH_MAX_LEN];
    char name[M_VFS_NAME_MAX_LEN];
    devfs_event_mask_t ready_mask;
    size_t notify_count;
    size_t poll_count;
    size_t blocked_count;
    size_t waiter_count;
    size_t shm_used;
    size_t shm_capacity;
    size_t unregister_events;
    bool tty_echo;
    bool tty_canonical;
    bool pty_is_slave;
    char pty_peer[M_VFS_PATH_MAX_LEN];
} devfs_device_info_t;

typedef struct {
    m_vfs_error_t (*open)(void *private_data);
    m_vfs_error_t (*close)(void *private_data);
    m_vfs_error_t (*read)(void *private_data,
                         void *buffer,
                         size_t size,
                         size_t *read);
    m_vfs_error_t (*write)(void *private_data,
                          const void *buffer,
                          size_t size,
                          size_t *written);
    m_vfs_error_t (*ioctl)(void *private_data,
                          unsigned long request,
                          void *arg);
    uint32_t (*poll)(void *private_data);
    m_vfs_error_t (*flush)(void *private_data);
    m_vfs_error_t (*reset)(void *private_data);
    m_vfs_error_t (*get_info)(void *private_data,
                              devfs_device_info_t *info);
    void (*destroy)(void *private_data);
} devfs_ops_t;

/**
 * @brief Notify DevFS that a device’s state has changed.
 *
 * Drivers should call this whenever their readiness mask changes.
 */
void devfs_notify(m_vfs_node_t *node, devfs_event_mask_t events);

/**
 * @brief Query the cached readiness mask for a device node.
 */
devfs_event_mask_t devfs_event_mask(const m_vfs_node_t *node);

/**
 * @brief Track that a poll iteration sampled this node.
 */
void devfs_record_poll(const m_vfs_node_t *node);

const m_vfs_fs_type_t *m_devfs_fs_type(void);

#if CONFIG_MAGNOLIA_VFS_DEVFS
void m_devfs_register_default_devices(void);
m_vfs_error_t devfs_register(const char *path,
                             const devfs_ops_t *ops,
                             void *private_data);
m_vfs_error_t devfs_register_ext(const char *path,
                                 const devfs_ops_t *ops,
                                 void *private_data,
                                 void (*node_attach)(const devfs_entry_t *,
                                                     devfs_device_node_t *),
                                 void (*node_detach)(const devfs_entry_t *,
                                                     devfs_device_node_t *));
m_vfs_error_t devfs_unregister(const char *path);
#else
static inline void m_devfs_register_default_devices(void) {}
static inline m_vfs_error_t devfs_register(const char *path,
                                           const devfs_ops_t *ops,
                                           void *private_data)
{
    (void)path;
    (void)ops;
    (void)private_data;
    return M_VFS_ERR_NOT_SUPPORTED;
}
static inline m_vfs_error_t devfs_unregister(const char *path)
{
    (void)path;
    return M_VFS_ERR_NOT_SUPPORTED;
}
static inline void devfs_notify(m_vfs_node_t *node, devfs_event_mask_t events)
{
    (void)node;
    (void)events;
}
static inline devfs_event_mask_t devfs_event_mask(const m_vfs_node_t *node)
{
    (void)node;
    return 0;
}
static inline void devfs_record_poll(const m_vfs_node_t *node)
{
    (void)node;
}
#endif

#ifdef __cplusplus
}
#endif

#endif /* MAGNOLIA_VFS_DEVFS_H */
