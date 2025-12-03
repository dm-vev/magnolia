#include "kernel/core/ipc/ipc_channel.h"
/*
 * Magnolia OS — IPC Subsystem
 * Purpose:
 *     Entry-point that initializes the IPC primitives.
 *
 * © 2025 Magnolia Project
 */

#include "kernel/core/ipc/ipc.h"

void ipc_init(void)
{
    ipc_core_init();
    ipc_signal_module_init();
    ipc_event_flags_module_init();
    m_ipc_channel_module_init();
    ipc_shm_module_init();
}
