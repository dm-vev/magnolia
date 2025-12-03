/**
 * @file        ipc_shm_private.h
 * @brief       Internal shared memory definitions used by the IPC implementation.
 * @details     Contains the region state, waiter helpers, and statistics that
 *              back the public shared memory APIs.
 */

#ifndef MAGNOLIA_IPC_SHM_PRIVATE_H
#define MAGNOLIA_IPC_SHM_PRIVATE_H

#include <stddef.h>
#include <stdint.h>

#include "kernel/core/ipc/ipc_core.h"
#include "kernel/core/ipc/ipc_scheduler_bridge.h"
#include "kernel/core/ipc/ipc_shm.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief   Internal waiter context used when threads block on shared memory.
 */
typedef struct {
    ipc_waiter_t waiter;
    size_t requested;
} ipc_shm_waiter_t;

/**
 * @brief   Header stored before each packet in packet-mode regions.
 */
typedef struct {
    uint16_t length;
} ipc_shm_packet_header_t;

/**
 * @brief   Runtime statistics collected per shared memory region.
 * @details Tracks reads, writes, timeout events, overflows, and attachments.
 */
typedef struct {
    size_t reads;
    size_t writes;
    size_t timed_reads;
    size_t timed_writes;
    size_t read_timeouts;
    size_t write_timeouts;
    size_t ring_overflows;
    size_t packet_drops;
    size_t attachments;
} ipc_shm_stats_t;

/**
 * @brief   Internal descriptor describing an allocated shared memory region.
 * @details Contains bookkeeping for raw/ring/packet modes, wait queues, cursors,
 *          and statistics.
 */
typedef struct {
    ipc_object_header_t header;
    ipc_shm_mode_t mode;
    size_t region_size;
    void *memory;
    ipc_shm_ring_overwrite_policy_t ring_policy;
    size_t attachment_count;
    size_t waiting_readers;
    size_t waiting_writers;
    ipc_wait_queue_t read_waiters;
    ipc_wait_queue_t write_waiters;
    size_t ring_head;
    size_t ring_tail;
    size_t ring_used;
    size_t packet_head;
    size_t packet_tail;
    size_t packet_count;
    size_t packet_bytes;
    size_t packet_max_payload;
    bool raw_ready;
    ipc_shm_stats_t stats;
} ipc_shm_region_t;

/**
 * @brief   Locate the region descriptor corresponding to an IPC handle.
 *
 * @param   handle          Shared memory handle to resolve.
 * @return  Region descriptor if the handle is valid or NULL otherwise.
 */
ipc_shm_region_t *ipc_shm_lookup(ipc_handle_t handle);

/**
 * @brief   Prepare internal region state before use.
 */
void ipc_shm_module_init(void);

#ifdef __cplusplus
}
#endif

#endif /* MAGNOLIA_IPC_SHM_PRIVATE_H */
