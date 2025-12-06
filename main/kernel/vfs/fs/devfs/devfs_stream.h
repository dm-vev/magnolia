#ifndef MAGNOLIA_VFS_DEVFS_STREAM_H
#define MAGNOLIA_VFS_DEVFS_STREAM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "freertos/portmacro.h"
#include "kernel/core/ipc/ipc_shm.h"
#include "kernel/core/vfs/m_vfs_types.h"
#include "kernel/vfs/fs/devfs/devfs.h"
#include "kernel/vfs/fs/devfs/devfs_shm.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const char *path;
    size_t buffer_capacity;
    ipc_handle_t handle;
    ipc_shm_attachment_t reader;
    ipc_shm_attachment_t writer;
    m_vfs_node_t *node;
    portMUX_TYPE lock;
    devfs_event_mask_t ready_mask;
    ipc_shm_ring_overwrite_policy_t policy;
} devfs_stream_context_t;

bool devfs_stream_context_init(devfs_stream_context_t *ctx,
                               const char *path,
                               size_t buffer_size,
                               ipc_shm_ring_overwrite_policy_t policy);
void devfs_stream_context_cleanup(devfs_stream_context_t *ctx);
void devfs_stream_attach_node(devfs_stream_context_t *ctx, m_vfs_node_t *node);
void devfs_stream_detach_node(devfs_stream_context_t *ctx);

m_vfs_error_t devfs_stream_control(devfs_stream_context_t *ctx,
                                   ipc_shm_control_command_t cmd,
                                   void *arg);

m_vfs_error_t devfs_stream_try_read(devfs_stream_context_t *ctx,
                                    void *buffer,
                                    size_t size,
                                    size_t *read);
m_vfs_error_t devfs_stream_try_write(devfs_stream_context_t *ctx,
                                     const void *buffer,
                                     size_t size,
                                     size_t *written);
m_vfs_error_t devfs_stream_read_timed(devfs_stream_context_t *ctx,
                                      void *buffer,
                                      size_t size,
                                      size_t *read,
                                      uint64_t timeout_us);
m_vfs_error_t devfs_stream_write_timed(devfs_stream_context_t *ctx,
                                       const void *buffer,
                                       size_t size,
                                       size_t *written,
                                       uint64_t timeout_us);

uint32_t devfs_stream_poll(const devfs_stream_context_t *ctx);
devfs_event_mask_t devfs_stream_ready_mask(const devfs_stream_context_t *ctx);
m_vfs_error_t devfs_stream_buffer_info(const devfs_stream_context_t *ctx,
                                       devfs_shm_buffer_info_t *info);

bool devfs_stream_register_pipes(void);
bool devfs_stream_register_ttys(void);
bool devfs_stream_register_ptys(void);

#ifdef __cplusplus
}
#endif

#endif /* MAGNOLIA_VFS_DEVFS_STREAM_H */
