#ifndef MAGNOLIA_ALLOC_TESTS_H
#define MAGNOLIA_ALLOC_TESTS_H

#include "sdkconfig.h"

#if CONFIG_MAGNOLIA_ALLOC_ENABLED && CONFIG_MAGNOLIA_ALLOC_SELFTESTS
void m_alloc_selftests_run(void);
#else
static inline void m_alloc_selftests_run(void) {}
#endif

#endif /* MAGNOLIA_ALLOC_TESTS_H */
