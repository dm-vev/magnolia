/*
 * Magnolia OS — IPC Subsystem
 * Purpose:
 *     Self-test helpers for the shared memory primitive.
 *
 * © 2025 Magnolia Project
 */

#ifndef MAGNOLIA_IPC_SHM_TESTS_H
#define MAGNOLIA_IPC_SHM_TESTS_H

#include "sdkconfig.h"

#ifdef CONFIG_MAGNOLIA_IPC_SELFTESTS
bool ipc_shm_tests_run(void);
#else
static inline bool ipc_shm_tests_run(void)
{
    return true;
}
#endif

#endif /* MAGNOLIA_IPC_SHM_TESTS_H */
