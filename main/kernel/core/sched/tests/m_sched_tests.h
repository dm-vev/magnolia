#ifndef MAGNOLIA_SCHED_TESTS_H
#define MAGNOLIA_SCHED_TESTS_H

#include "sdkconfig.h"

#ifdef CONFIG_MAGNOLIA_SCHED_SELFTESTS
void m_sched_selftests_run(void);
#else
static inline void m_sched_selftests_run(void) {}
#endif

#endif /* MAGNOLIA_SCHED_TESTS_H */
