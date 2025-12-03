/*
 * Magnolia OS — IPC Subsystem
 * Purpose:
 *     Self-test helpers for the Magnolia IPC channel primitive.
 *
 * © 2025 Magnolia Project
 */

#ifndef MAGNOLIA_IPC_CHANNEL_TESTS_H
#define MAGNOLIA_IPC_CHANNEL_TESTS_H

#include "sdkconfig.h"

#if CONFIG_MAGNOLIA_IPC_ENABLED && CONFIG_MAGNOLIA_IPC_SELFTESTS
bool ipc_channel_tests_run(void);
#else
static inline bool ipc_channel_tests_run(void)
{
    return true;
}
#endif

#endif /* MAGNOLIA_IPC_CHANNEL_TESTS_H */
