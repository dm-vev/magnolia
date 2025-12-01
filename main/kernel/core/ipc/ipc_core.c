#include <string.h>

#include "kernel/core/ipc/ipc_core.h"

static portMUX_TYPE g_ipc_registry_lock = portMUX_INITIALIZER_UNLOCKED;

static uint16_t g_signal_generations[IPC_MAX_SIGNALS];
static bool g_signal_alloc[IPC_MAX_SIGNALS];

static ipc_handle_registry_t g_signal_registry = {
    .type = IPC_OBJECT_SIGNAL,
    .capacity = IPC_MAX_SIGNALS,
    .generation = g_signal_generations,
    .allocated = g_signal_alloc,
};

void ipc_core_init(void)
{
    memset(g_signal_generations, 0, sizeof(g_signal_generations));
    memset(g_signal_alloc, 0, sizeof(g_signal_alloc));
}

ipc_handle_t ipc_handle_make(ipc_object_type_t type,
                             uint16_t index,
                             uint16_t generation)
{
    return ((ipc_handle_t)((uint32_t)generation & IPC_HANDLE_GEN_MASK)
            << IPC_HANDLE_GEN_SHIFT)
           | ((ipc_handle_t)((uint32_t)type & IPC_HANDLE_TYPE_MASK)
              << IPC_HANDLE_TYPE_SHIFT)
           | ((ipc_handle_t)index & IPC_HANDLE_INDEX_MASK);
}

bool ipc_handle_unpack(ipc_handle_t handle,
                       ipc_object_type_t *out_type,
                       uint16_t *out_index,
                       uint16_t *out_generation)
{
    if (handle == IPC_HANDLE_INVALID) {
        return false;
    }

    if (out_type != NULL) {
        *out_type = (ipc_object_type_t)((handle >> IPC_HANDLE_TYPE_SHIFT)
                                         & IPC_HANDLE_TYPE_MASK);
    }
    if (out_index != NULL) {
        *out_index = (uint16_t)(handle & IPC_HANDLE_INDEX_MASK);
    }
    if (out_generation != NULL) {
        *out_generation = (uint16_t)((handle >> IPC_HANDLE_GEN_SHIFT)
                                     & IPC_HANDLE_GEN_MASK);
    }

    return true;
}

ipc_error_t ipc_handle_allocate(ipc_handle_registry_t *registry,
                                uint16_t *out_index,
                                ipc_handle_t *out_handle)
{
    if (registry == NULL || out_index == NULL || out_handle == NULL
        || registry->capacity == 0 || registry->generation == NULL
        || registry->allocated == NULL) {
        return IPC_ERR_INVALID_ARGUMENT;
    }

    ipc_error_t result = IPC_ERR_NO_SPACE;
    portENTER_CRITICAL(&g_ipc_registry_lock);

    for (uint16_t idx = 0; idx < registry->capacity; idx++) {
        if (!registry->allocated[idx]) {
            registry->allocated[idx] = true;
            registry->generation[idx] = (registry->generation[idx] + 1)
                                        & IPC_HANDLE_GEN_MASK;
            if (registry->generation[idx] == 0) {
                registry->generation[idx] = 1;
            }

            *out_index = idx;
            *out_handle = ipc_handle_make(registry->type,
                                          idx,
                                          registry->generation[idx]);
            result = IPC_OK;
            break;
        }
    }

    portEXIT_CRITICAL(&g_ipc_registry_lock);
    return result;
}

void ipc_handle_release(ipc_handle_registry_t *registry, uint16_t index)
{
    if (registry == NULL || registry->allocated == NULL) {
        return;
    }

    portENTER_CRITICAL(&g_ipc_registry_lock);
    if (index < registry->capacity) {
        registry->allocated[index] = false;
    }
    portEXIT_CRITICAL(&g_ipc_registry_lock);
}

ipc_handle_registry_t *ipc_core_signal_registry(void)
{
    return &g_signal_registry;
}
