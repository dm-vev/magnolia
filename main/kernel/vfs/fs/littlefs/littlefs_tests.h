#ifndef MAGNOLIA_VFS_LITTLEFS_TESTS_H
#define MAGNOLIA_VFS_LITTLEFS_TESTS_H

#include "sdkconfig.h"

#if CONFIG_MAGNOLIA_LITTLEFS_ENABLED && CONFIG_MAGNOLIA_VFS_LITTLEFS_SELFTESTS
void littlefs_selftests_run(void);
#else
static inline void littlefs_selftests_run(void) {}
#endif

#endif /* MAGNOLIA_VFS_LITTLEFS_TESTS_H */

