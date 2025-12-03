#include "kernel/core/ipc/ipc_channel.h"
#include "kernel/core/ipc/ipc.h"

#if CONFIG_MAGNOLIA_IPC_ENABLED

void ipc_init(void)
{
    ipc_core_init();
    ipc_signal_module_init();
    ipc_event_flags_module_init();
    m_ipc_channel_module_init();
    ipc_shm_module_init();
}

#else

void ipc_init(void)
{
}

#endif
