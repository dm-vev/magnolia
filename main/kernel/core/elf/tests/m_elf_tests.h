#ifndef MAGNOLIA_ELF_M_ELF_TESTS_H
#define MAGNOLIA_ELF_M_ELF_TESTS_H

#include "sdkconfig.h"

#ifdef __cplusplus
extern "C" {
#endif

#if CONFIG_MAGNOLIA_ELF_ENABLED && CONFIG_MAGNOLIA_ELF_SELFTESTS
void m_elf_selftests_run(void);
#else
static inline void m_elf_selftests_run(void) {}
#endif

#ifdef __cplusplus
}
#endif

#endif /* MAGNOLIA_ELF_M_ELF_TESTS_H */

