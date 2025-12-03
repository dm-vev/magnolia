#/*
 * Magnolia OS — IPC Subsystem
 * Purpose:
 *     Handle layout, registries, and error definitions shared across primitives.
 *
 * © 2025 Magnolia Project
 */

#ifndef MAGNOLIA_IPC_CORE_H
#define MAGNOLIA_IPC_CORE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "sdkconfig.h"
#include "freertos/portmacro.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Handle layout constants.
 */
#define IPC_HANDLE_INDEX_BITS 12U
#define IPC_HANDLE_INDEX_MASK ((1U << IPC_HANDLE_INDEX_BITS) - 1U)
#define IPC_HANDLE_TYPE_SHIFT IPC_HANDLE_INDEX_BITS
#define IPC_HANDLE_TYPE_MASK 0x0FU
#define IPC_HANDLE_GEN_SHIFT 16U
#define IPC_HANDLE_GEN_MASK 0xFFFFU

/**
 * @brief Invalid handle sentinel.
 */
#define IPC_HANDLE_INVALID 0U

/**
 * @brief Magnolia IPC handle type.
 */
typedef uint32_t ipc_handle_t;

/**
 * @brief Maximum number of signals Magnolia IPC exposes.
 */
#define IPC_MAX_SIGNALS CONFIG_MAGNOLIA_IPC_MAX_SIGNALS

/**
 * @brief Maximum number of message channels Magnolia IPC exposes.
 */
#define IPC_MAX_CHANNELS CONFIG_MAGNOLIA_IPC_MAX_CHANNELS

#define IPC_MAX_EVENT_FLAGS CONFIG_MAGNOLIA_IPC_MAX_EVENT_FLAGS
#define IPC_MAX_SHM_REGIONS CONFIG_MAGNOLIA_IPC_MAX_SHM_REGIONS

/**
 * @brief Magnolai IPC error codes shared across primitives.
 */
typedef enum {
    IPC_OK = 0,
    IPC_ERR_INVALID_HANDLE,
    IPC_ERR_INVALID_ARGUMENT,
    IPC_ERR_OBJECT_DESTROYED,
    IPC_ERR_TIMEOUT,
    IPC_ERR_NOT_READY,
    IPC_ERR_NO_SPACE,
    IPC_ERR_SHUTDOWN,
    IPC_ERR_WOULD_BLOCK,
    IPC_ERR_NO_PERMISSION,
    IPC_ERR_FULL,
    IPC_ERR_EMPTY,
    IPC_ERR_NOT_ATTACHED,
    IPC_ERR_NOT_SUPPORTED,
} ipc_error_t;

/**
 * @brief List of IPC object kinds.
 */
typedef enum {
    IPC_OBJECT_NONE = 0,
    IPC_OBJECT_SIGNAL = 1,
    IPC_OBJECT_CHANNEL = 2,
    IPC_OBJECT_EVENT_FLAGS = 3,
    IPC_OBJECT_SHM_REGION = 4,
    IPC_OBJECT_TYPE_COUNT,
} ipc_object_type_t;

/**
 * @brief Base header stored in each IPC object.
 */
typedef struct {
    portMUX_TYPE lock;
    ipc_handle_t handle;
    ipc_object_type_t type;
    uint16_t generation;
    bool destroyed;
    size_t waiting_tasks;
} ipc_object_header_t;

/**
 * @brief Lightweight registry describing object slots.
 */
typedef struct {
    ipc_object_type_t type;
    size_t capacity;
    uint16_t *generation;
    bool *allocated;
} ipc_handle_registry_t;

void ipc_core_init(void);
ipc_handle_t ipc_handle_make(ipc_object_type_t type,
                             uint16_t index,
                             uint16_t generation);
bool ipc_handle_unpack(ipc_handle_t handle,
                       ipc_object_type_t *out_type,
                       uint16_t *out_index,
                       uint16_t *out_generation);
ipc_error_t ipc_handle_allocate(ipc_handle_registry_t *registry,
                                uint16_t *out_index,
                                ipc_handle_t *out_handle);
void ipc_handle_release(ipc_handle_registry_t *registry,
                        uint16_t index);
ipc_handle_registry_t *ipc_core_signal_registry(void);
ipc_handle_registry_t *ipc_core_channel_registry(void);
ipc_handle_registry_t *ipc_core_event_flags_registry(void);
ipc_handle_registry_t *ipc_core_shm_registry(void);

#ifdef __cplusplus
}
#endif

#endif /* MAGNOLIA_IPC_CORE_H */
#if CONFIG_MAGNOLIA_IPC_CHANNEL_DEFAULT_CAPACITY \
        > CONFIG_MAGNOLIA_IPC_CHANNEL_CAPACITY_MAX
#error "Default channel depth must not exceed channel depth maximum"
#endif

#if CONFIG_MAGNOLIA_IPC_CHANNEL_DEFAULT_MESSAGE_SIZE \
        > CONFIG_MAGNOLIA_IPC_CHANNEL_MAX_MESSAGE_SIZE
#error "Default channel message size must not exceed channel message size maximum"
#endif
