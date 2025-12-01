#ifndef MAGNOLIA_IPC_SIGNAL_TESTS_H
#define MAGNOLIA_IPC_SIGNAL_TESTS_H

#include "sdkconfig.h"

#ifdef CONFIG_MAGNOLIA_IPC_SELFTESTS
void ipc_selftests_run(void);
#else
static inline void ipc_selftests_run(void) { }
#endif

#endif /* MAGNOLIA_IPC_SIGNAL_TESTS_H */
