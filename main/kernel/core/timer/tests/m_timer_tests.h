#ifndef MAGNOLIA_TIMER_TESTS_H
#define MAGNOLIA_TIMER_TESTS_H

#include "sdkconfig.h"

#ifdef CONFIG_MAGNOLIA_TIMER_SELFTESTS
void m_timer_selftests_run(void);
#else
static inline void m_timer_selftests_run(void) {}
#endif

#endif /* MAGNOLIA_TIMER_TESTS_H */
