#include "kernel/arch/m_arch.h"

#include <stddef.h>
#include <stdint.h>

void m_arch_task_init_stack(void *stack_top,
                            size_t stack_size,
                            void (*entry)(void *),
                            void *arg)
{
    (void)stack_top;
    (void)stack_size;
    (void)entry;
    (void)arg;
}

void m_arch_cache_flush(void *addr, size_t size)
{
    (void)addr;
    (void)size;
    m_arch_memory_barrier();
}

void m_arch_cache_invalidate(void *addr, size_t size)
{
    (void)addr;
    (void)size;
    m_arch_memory_barrier();
}

void m_arch_cache_barrier(void)
{
    m_arch_memory_barrier();
}

void m_arch_memory_barrier(void)
{
    __sync_synchronize();
}

void m_arch_dcache_enable(void) {}

void m_arch_dcache_disable(void) {}

void m_arch_icache_enable(void) {}

void m_arch_icache_disable(void) {}

int32_t m_arch_atomic_add(volatile int32_t *ptr, int32_t value)
{
    return __atomic_add_fetch(ptr, value, __ATOMIC_SEQ_CST);
}

int32_t m_arch_atomic_cmpxchg(volatile int32_t *ptr,
                              int32_t expected,
                              int32_t desired)
{
    int32_t actual = expected;
    __atomic_compare_exchange_n(ptr,
                                &actual,
                                desired,
                                false,
                                __ATOMIC_SEQ_CST,
                                __ATOMIC_SEQ_CST);
    return actual;
}
