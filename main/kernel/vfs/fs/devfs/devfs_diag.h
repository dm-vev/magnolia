#ifndef MAGNOLIA_VFS_DEVFS_DIAG_H
#define MAGNOLIA_VFS_DEVFS_DIAG_H

#include <stddef.h>
#include <stdbool.h>

#include "sdkconfig.h"
#include "kernel/core/job/jctx_public.h"
#include "kernel/vfs/fs/devfs/devfs.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef devfs_device_info_t devfs_diag_device_info_t;
typedef bool (*devfs_diag_device_iter_fn)(const devfs_diag_device_info_t *info,
                                          void *user_data);

typedef struct {
    m_job_id_t job;
    size_t waiter_count;
    devfs_event_mask_t ready_mask;
    char path[M_VFS_PATH_MAX_LEN];
} devfs_diag_waiter_info_t;

typedef bool (*devfs_diag_waiter_iter_fn)(const devfs_diag_waiter_info_t *info,
                                          void *user_data);

typedef struct {
    const char *path;
    size_t used;
    size_t capacity;
    devfs_event_mask_t ready_mask;
} devfs_diag_shm_info_t;

typedef bool (*devfs_diag_shm_iter_fn)(const devfs_diag_shm_info_t *info,
                                       void *user_data);

typedef bool (*devfs_diag_tree_iter_fn)(const m_vfs_node_t *node,
                                        void *user_data);

#if CONFIG_MAGNOLIA_VFS_DEVFS
void devfs_diag_device_iterate(devfs_diag_device_iter_fn cb, void *user_data);
void devfs_diag_tree_snapshot(devfs_diag_tree_iter_fn cb, void *user_data);
void devfs_diag_waiters(devfs_diag_waiter_iter_fn cb, void *user_data);
void devfs_diag_shm_info(devfs_diag_shm_iter_fn cb, void *user_data);
size_t devfs_diag_unregister_events(void);
size_t devfs_diag_total_poll_count(void);
#else
static inline void
devfs_diag_device_iterate(devfs_diag_device_iter_fn cb, void *user_data)
{
    (void)cb;
    (void)user_data;
}

static inline void
devfs_diag_tree_snapshot(devfs_diag_tree_iter_fn cb, void *user_data)
{
    (void)cb;
    (void)user_data;
}

static inline void
devfs_diag_waiters(devfs_diag_waiter_iter_fn cb, void *user_data)
{
    (void)cb;
    (void)user_data;
}

static inline void
devfs_diag_shm_info(devfs_diag_shm_iter_fn cb, void *user_data)
{
    (void)cb;
    (void)user_data;
}

static inline size_t
devfs_diag_unregister_events(void)
{
    return 0;
}

static inline size_t
devfs_diag_total_poll_count(void)
{
    return 0;
}
#endif

#ifdef __cplusplus
}
#endif

#endif /* MAGNOLIA_VFS_DEVFS_DIAG_H */
