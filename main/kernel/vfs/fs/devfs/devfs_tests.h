#ifndef MAGNOLIA_VFS_DEVFS_TESTS_H
#define MAGNOLIA_VFS_DEVFS_TESTS_H

#include "sdkconfig.h"

#if CONFIG_MAGNOLIA_VFS_DEVFS && CONFIG_MAGNOLIA_DEVFS_SELFTESTS
void devfs_selftests_run(void);
#else
static inline void devfs_selftests_run(void) {}
#endif

#endif /* MAGNOLIA_VFS_DEVFS_TESTS_H */
