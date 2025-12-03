/**
 * @file        ipc_shm.h
 * @brief       Public shared memory primitives for the Magnolia IPC subsystem.
 * @details     Defines region lifecycle, attachment semantics, and diagnostics helpers
 *              for raw, ring-buffer, and packet-oriented shared memory modes.
 */

#ifndef MAGNOLIA_IPC_SHM_H
#define MAGNOLIA_IPC_SHM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "kernel/core/ipc/ipc_core.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief   Supported shared memory modes.
 */
typedef enum {
    IPC_SHM_MODE_RAW = 0,
    IPC_SHM_MODE_RING_BUFFER,
    IPC_SHM_MODE_PACKET_BUFFER,
} ipc_shm_mode_t;

/**
 * @brief   Attachment access flags.
 */
typedef enum {
    IPC_SHM_ACCESS_READ_ONLY = 0,
    IPC_SHM_ACCESS_WRITE_ONLY,
    IPC_SHM_ACCESS_READ_WRITE,
} ipc_shm_access_mode_t;

/**
 * @brief   Ring-buffer overwrite policies.
 */
typedef enum {
    IPC_SHM_RING_OVERWRITE_BLOCK = 0,
    IPC_SHM_RING_OVERWRITE_DROP_OLDEST,
} ipc_shm_ring_overwrite_policy_t;

/**
 * @brief   Commands executed via ipc_shm_control().
 */
typedef enum {
    IPC_SHM_CONTROL_FLUSH = 0,
    IPC_SHM_CONTROL_RESET,
    IPC_SHM_CONTROL_NOTIFY_READERS,
    IPC_SHM_CONTROL_NOTIFY_WRITERS,
    IPC_SHM_CONTROL_GET_INFO,
} ipc_shm_control_command_t;

/**
 * @brief   Region creation options for non-raw modes.
 * @details Allows configuring ring overwrite and maximum packet payload
 *          before the region is allocated.
 */
typedef struct {
    ipc_shm_ring_overwrite_policy_t ring_policy;
    size_t packet_max_payload;
} ipc_shm_region_options_t;

/**
 * @brief   Attachment creation metadata.
 * @details Provides a cursor offset for raw-mode clients to resume
 *          from a specific byte within the region.
 */
typedef struct {
    size_t cursor_offset;
} ipc_shm_attachment_options_t;

/**
 * @brief   Runtime descriptor returned to clients when they attach.
 * @details Tracks the handle, cursor, permission set, and the backing
 *          region so public APIs can validate operations.
 */
typedef struct {
    ipc_handle_t handle;
    ipc_shm_access_mode_t mode;
    size_t cursor;
    bool attached;
    void *internal;
} ipc_shm_attachment_t;

/**
 * @brief   Diagnostic snapshot for a shared memory region.
 * @details Populated via ipc_shm_query() so callers can inspect capacity,
 *          waiters, and statistics without racing against payload data.
 */
typedef struct {
    size_t region_size;
    ipc_shm_mode_t mode;
    size_t attachment_count;
    size_t waiting_readers;
    size_t waiting_writers;
    bool destroyed;
    size_t ring_capacity;
    size_t ring_used;
    size_t ring_overflows;
    size_t packet_inflight;
    size_t packet_drops;
} ipc_shm_info_t;

/**
 * @brief   Prepare the shared memory subsystem for use.
 * @details Clears the internal region table and reinitializes the per-region
 *          locks without allocating any memory.
 *
 * @return  void Initialization always succeeds.
 */
void ipc_shm_module_init(void);

/**
 * @brief   Create a shared memory region handle.
 * @details Allocates memory for the requested mode and registers the handle so
 *          clients can attach and exchange data deterministically.
 *
 * @param   size            Region size in bytes.
 * @param   mode            Memory layout mode.
 * @param   options         Optional mode-specific parameters.
 * @param   out_handle      Receives the new region handle.
 *
 * @return  IPC_OK          Region created successfully.
 * @return  IPC_ERR_INVALID_ARGUMENT
 *                         Invalid size, mode, or null handle pointer.
 * @return  IPC_ERR_NO_SPACE Not enough region slots or heap memory.
 */
ipc_error_t ipc_shm_create(size_t size,
                           ipc_shm_mode_t mode,
                           const ipc_shm_region_options_t *options,
                           ipc_handle_t *out_handle);

/**
 * @brief   Destroy a shared memory region.
 * @details Wakes waiters with IPC_ERR_OBJECT_DESTROYED and releases memory once
 *          every attachment is detached.
 *
 * @param   handle          Region handle to destroy.
 *
 * @return  IPC_OK          Region destroyed successfully.
 * @return  IPC_ERR_INVALID_HANDLE
 *                         Handle does not point to a live region.
 * @return  IPC_ERR_OBJECT_DESTROYED
 *                         Region was already destroyed.
 */
ipc_error_t ipc_shm_destroy(ipc_handle_t handle);

/**
 * @brief   Attach to a shared memory region.
 * @details Increases the attachment count and initializes the cursor according
 *          to the provided options so clients can start issuing read/write calls.
 *
 * @param   handle          Region handle to attach to.
 * @param   access          Desired access rights.
 * @param   options         Optional attachment metadata.
 * @param   out_attachment  Receives the attachment descriptor.
 *
 * @return  IPC_OK          Attachment succeeded.
 * @return  IPC_ERR_INVALID_ARGUMENT
 *                         Null descriptor or unsupported access flag.
 * @return  IPC_ERR_INVALID_HANDLE
 *                         Handle is invalid or refers to another object.
 * @return  IPC_ERR_OBJECT_DESTROYED
 *                         Region was destroyed before attachment could complete.
 */
ipc_error_t ipc_shm_attach(ipc_handle_t handle,
                          ipc_shm_access_mode_t access,
                          const ipc_shm_attachment_options_t *options,
                          ipc_shm_attachment_t *out_attachment);

/**
 * @brief   Release a shared memory attachment.
 * @details Decrements the attachment count and releases the handle once no
 *          attachments remain.
 *
 * @param   attachment      Attachment descriptor to detach.
 *
 * @return  IPC_OK          Detached successfully.
 * @return  IPC_ERR_NOT_ATTACHED
 *                         Descriptor is not attached or was already detached.
 * @return  IPC_ERR_INVALID_HANDLE
 *                         Handle does not match the underlying region.
 */
ipc_error_t ipc_shm_detach(ipc_shm_attachment_t *attachment);

/**
 * @brief   Blocking read from a shared memory region.
 * @details The caller waits until payload is available, the region is destroyed,
 *          or the waiter is woken by control operations.
 *
 * @param   attachment      Attachment used for the read.
 * @param   out_buffer      Buffer receiving data.
 * @param   buffer_size     Maximum bytes to read.
 * @param   out_transferred Optional pointer receiving actual byte count.
 *
 * @return  IPC_OK          Data was read successfully.
 * @return  IPC_ERR_INVALID_ARGUMENT
 *                         Attachment or buffer is null or buffer_size is zero.
 * @return  IPC_ERR_NOT_ATTACHED
 *                         Descriptor does not refer to an attachment.
 * @return  IPC_ERR_INVALID_HANDLE
 *                         Attachment handle is stale.
 * @return  IPC_ERR_NO_PERMISSION
 *                         Attachment lacks read permission.
 * @return  IPC_ERR_OBJECT_DESTROYED
 *                         Region was destroyed while waiting.
 * @return  IPC_ERR_EMPTY    No data was available and the operation would block.
 * @return  IPC_ERR_SHUTDOWN Shared memory subsystem is shutting down.
 */
ipc_error_t ipc_shm_read(ipc_shm_attachment_t *attachment,
                         void *out_buffer,
                         size_t buffer_size,
                         size_t *out_transferred);

/**
 * @brief   Blocking write to a shared memory region.
 * @details The caller waits until space is available according to the selected
 *          ring policy or until the region is destroyed.
 *
 * @param   attachment      Attachment used for the write.
 * @param   data            Source buffer.
 * @param   length          Number of bytes to write.
 *
 * @return  IPC_OK          Data was written successfully.
 * @return  IPC_ERR_INVALID_ARGUMENT
 *                         Null pointers or unsupported length for the mode.
 * @return  IPC_ERR_NOT_ATTACHED
 *                         Descriptor is not attached.
 * @return  IPC_ERR_INVALID_HANDLE
 *                         Attachment handle is stale.
 * @return  IPC_ERR_NO_PERMISSION
 *                         Attachment is read-only.
 * @return  IPC_ERR_OBJECT_DESTROYED
 *                         Region was destroyed while waiting.
 * @return  IPC_ERR_FULL     Buffer or ring cannot accept the length payload.
 * @return  IPC_ERR_SHUTDOWN Shared memory subsystem is shutting down.
 */
ipc_error_t ipc_shm_write(ipc_shm_attachment_t *attachment,
                          const void *data,
                          size_t length);

/**
 * @brief   Timed read from a shared memory region.
 * @details Same as ipc_shm_read() but returns IPC_ERR_TIMEOUT when the
 *          timeout expires without payload.
 *
 * @param   attachment      Attachment used for the read.
 * @param   out_buffer      Buffer receiving data.
 * @param   buffer_size     Maximum bytes to read.
 * @param   out_transferred Optional pointer receiving actual byte count.
 * @param   timeout_us      Timeout in microseconds; M_TIMER_TIMEOUT_FOREVER
 *                          waits indefinitely.
 *
 * @return  IPC_OK          Data was read successfully.
 * @return  IPC_ERR_INVALID_ARGUMENT
 *                         Attachment or buffer is null, buffer_size zero, or
 *                         timeout_us is zero for timed operations.
 * @return  IPC_ERR_NOT_ATTACHED
 *                         Descriptor does not refer to an attachment.
 * @return  IPC_ERR_INVALID_HANDLE
 *                         Attachment handle is stale.
 * @return  IPC_ERR_NO_PERMISSION
 *                         Attachment lacks read permission.
 * @return  IPC_ERR_OBJECT_DESTROYED
 *                         Region was destroyed while waiting.
 * @return  IPC_ERR_EMPTY
 *                         No data was available before the timeout.
 * @return  IPC_ERR_TIMEOUT  Timeout expired before data arrived.
 * @return  IPC_ERR_SHUTDOWN Shared memory subsystem is shutting down.
 */
ipc_error_t ipc_shm_read_timed(ipc_shm_attachment_t *attachment,
                               void *out_buffer,
                               size_t buffer_size,
                               size_t *out_transferred,
                               uint64_t timeout_us);

/**
 * @brief   Timed write to a shared memory region.
 * @details Same as ipc_shm_write() but returns IPC_ERR_TIMEOUT when the
 *          timeout expires without space.
 *
 * @param   attachment      Attachment used for the write.
 * @param   data            Source buffer.
 * @param   length          Number of bytes to write.
 * @param   timeout_us      Timeout in microseconds; M_TIMER_TIMEOUT_FOREVER
 *                          waits indefinitely.
 *
 * @return  IPC_OK          Data was written successfully.
 * @return  IPC_ERR_INVALID_ARGUMENT
 *                         Null pointers, unsupported length, or zero timeout.
 * @return  IPC_ERR_NOT_ATTACHED
 *                         Descriptor is not attached.
 * @return  IPC_ERR_INVALID_HANDLE
 *                         Attachment handle is stale.
 * @return  IPC_ERR_NO_PERMISSION
 *                         Attachment is read-only.
 * @return  IPC_ERR_OBJECT_DESTROYED
 *                         Region was destroyed while waiting.
 * @return  IPC_ERR_FULL     No buffer space became available before the timeout.
 * @return  IPC_ERR_TIMEOUT  Timeout expired before space became writable.
 * @return  IPC_ERR_SHUTDOWN Shared memory subsystem is shutting down.
 */
ipc_error_t ipc_shm_write_timed(ipc_shm_attachment_t *attachment,
                                const void *data,
                                size_t length,
                                uint64_t timeout_us);

/**
 * @brief   Non-blocking read from a shared memory region.
 * @details Returns immediately if no data is available.
 *
 * @param   attachment      Attachment used for the read.
 * @param   out_buffer      Buffer receiving data.
 * @param   buffer_size     Maximum bytes to read.
 * @param   out_transferred Optional pointer receiving actual byte count.
 *
 * @return  IPC_OK          Data was read successfully.
 * @return  IPC_ERR_INVALID_ARGUMENT
 *                         Attachment or buffer is null or buffer_size zero.
 * @return  IPC_ERR_NOT_ATTACHED
 *                         Descriptor does not refer to an attachment.
 * @return  IPC_ERR_INVALID_HANDLE
 *                         Attachment handle is stale.
 * @return  IPC_ERR_NO_PERMISSION
 *                         Attachment lacks read permission.
 * @return  IPC_ERR_OBJECT_DESTROYED
 *                         Region was destroyed while the operation was issued.
 * @return  IPC_ERR_EMPTY    No data was available when the call returned.
 * @return  IPC_ERR_SHUTDOWN Shared memory subsystem is shutting down.
 */
ipc_error_t ipc_shm_try_read(ipc_shm_attachment_t *attachment,
                             void *out_buffer,
                             size_t buffer_size,
                             size_t *out_transferred);

/**
 * @brief   Non-blocking write to a shared memory region.
 * @details Returns immediately if no buffer space is available.
 *
 * @param   attachment      Attachment used for the write.
 * @param   data            Source buffer.
 * @param   length          Number of bytes to write.
 *
 * @return  IPC_OK          Data was written successfully.
 * @return  IPC_ERR_INVALID_ARGUMENT
 *                         Null pointers or unsupported length for the mode.
 * @return  IPC_ERR_NOT_ATTACHED
 *                         Descriptor is not attached.
 * @return  IPC_ERR_INVALID_HANDLE
 *                         Attachment handle is stale.
 * @return  IPC_ERR_NO_PERMISSION
 *                         Attachment is read-only.
 * @return  IPC_ERR_OBJECT_DESTROYED
 *                         Region was destroyed while the operation was issued.
 * @return  IPC_ERR_FULL     No buffer space was available when the call returned.
 * @return  IPC_ERR_SHUTDOWN Shared memory subsystem is shutting down.
 */
ipc_error_t ipc_shm_try_write(ipc_shm_attachment_t *attachment,
                              const void *data,
                              size_t length);

/**
 * @brief   Control operations for shared memory regions.
 * @details Flush, reset, and notification commands are serialized under the
 *          region lock and never block.
 *
 * @param   handle          Region handle targeted by the command.
 * @param   cmd             Control command.
 * @param   arg             Optional command argument.
 *
 * @return  IPC_OK          Command completed successfully.
 * @return  IPC_ERR_INVALID_ARGUMENT
 *                         Invalid command or missing argument for GET_INFO.
 * @return  IPC_ERR_INVALID_HANDLE
 *                         Handle does not resolve to a region.
 * @return  IPC_ERR_OBJECT_DESTROYED
 *                         Cannot control a destroyed region.
 */
ipc_error_t ipc_shm_control(ipc_handle_t handle,
                            ipc_shm_control_command_t cmd,
                            void *arg);

/**
 * @brief   Query diagnostic information for a region.
 *
 * @param   handle          Region handle to inspect.
 * @param   info            Receives the diagnostic snapshot.
 *
 * @return  IPC_OK          Snapshot populated successfully.
 * @return  IPC_ERR_INVALID_ARGUMENT
 *                         Null info pointer.
 * @return  IPC_ERR_INVALID_HANDLE
 *                         Handle does not resolve to a region.
 */
ipc_error_t ipc_shm_query(ipc_handle_t handle, ipc_shm_info_t *info);

#ifdef __cplusplus
}
#endif

#endif /* MAGNOLIA_IPC_SHM_H */
