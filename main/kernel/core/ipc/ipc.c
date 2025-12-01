#include "kernel/core/ipc/ipc_core.h"
#include "kernel/core/ipc/ipc_signal.h"

void ipc_init(void)
{
    ipc_core_init();
    ipc_signal_module_init();
}
