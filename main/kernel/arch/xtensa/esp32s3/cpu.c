#define M_ARCH_CPU_IMPL_WFI_INSTR() __asm__ volatile ("waiti 0")
#define M_ARCH_CPU_IMPL_WFE_INSTR() __asm__ volatile ("waiti 0")
#include "../../common/m_arch_cpu_impl.c"
