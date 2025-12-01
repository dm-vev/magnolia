#ifndef MAGNOLIA_JOB_TESTS_H
#define MAGNOLIA_JOB_TESTS_H

#include "sdkconfig.h"

#ifdef CONFIG_MAGNOLIA_JOB_SELFTESTS
void m_job_selftests_run(void);
#else
static inline void m_job_selftests_run(void) {}
#endif

#endif /* MAGNOLIA_JOB_TESTS_H */
