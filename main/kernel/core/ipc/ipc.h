/*
 * Magnolia OS — IPC Subsystem
 * Purpose:
 *     Public entrypoints and shared includes for the IPC primitives.
 *
 * © 2025 Magnolia Project
 */

#ifndef MAGNOLIA_IPC_H
#define MAGNOLIA_IPC_H

#include "kernel/core/ipc/ipc_channel.h"
#include "kernel/core/ipc/ipc_core.h"
#include "kernel/core/ipc/ipc_diag.h"
#include "kernel/core/ipc/ipc_event_flags.h"
#include "kernel/core/ipc/ipc_signal.h"
#include "kernel/core/ipc/ipc_shm.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize the Magnolia IPC subsystem.
 *
 * Sets up the core registries and primitive modules.
 */
void ipc_init(void);

#ifdef __cplusplus
}
#endif

#endif /* MAGNOLIA_IPC_H */
