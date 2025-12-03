#define M_ARCH_CPU_IMPL_WFI_INSTR() __asm__ volatile ("wfi")
#define M_ARCH_CPU_IMPL_WFE_INSTR() __asm__ volatile ("wfi")
#include "../../common/m_arch_cpu_impl.c"
