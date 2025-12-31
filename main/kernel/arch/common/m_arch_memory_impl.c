#include "kernel/arch/m_arch.h"

#include <stddef.h>
#include <stdint.h>

#include "esp_cache.h"
#include "esp_heap_caps.h"

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
    if (addr && size) {
        (void)esp_cache_msync(addr,
                              size,
                              ESP_CACHE_MSYNC_FLAG_DIR_C2M |
                                  ESP_CACHE_MSYNC_FLAG_UNALIGNED);
    }
    m_arch_memory_barrier();
}

void m_arch_cache_invalidate(void *addr, size_t size)
{
    if (addr && size) {
        size_t line = esp_cache_get_line_size_by_addr(addr);
        if (line) {
            uintptr_t start = (uintptr_t)addr;
            uintptr_t end = 0;
            if (!__builtin_add_overflow(start, size, &end)) {
                uintptr_t aligned_start = start & ~(uintptr_t)(line - 1u);
                uintptr_t aligned_end = (end + line - 1u) & ~(uintptr_t)(line - 1u);
                size_t aligned_size = (aligned_end > aligned_start)
                                          ? (size_t)(aligned_end - aligned_start)
                                          : 0;
                if (aligned_size) {
                    /* Sync instruction cache for freshly-written code. */
                    (void)esp_cache_msync((void *)aligned_start,
                                          aligned_size,
                                          ESP_CACHE_MSYNC_FLAG_DIR_M2C |
                                              ESP_CACHE_MSYNC_FLAG_TYPE_INST);
                }
            }
        }
    }
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

void *m_arch_malloc(size_t size)
{
    if (size == 0) {
        return NULL;
    }
    return heap_caps_malloc(size, MALLOC_CAP_DEFAULT);
}

void m_arch_free(void *ptr)
{
    if (ptr == NULL) {
        return;
    }
    heap_caps_free(ptr);
}

size_t m_arch_get_free_memory(void)
{
    return heap_caps_get_free_size(MALLOC_CAP_DEFAULT);
}

size_t m_arch_get_total_memory(void)
{
    return heap_caps_get_total_size(MALLOC_CAP_DEFAULT);
}
