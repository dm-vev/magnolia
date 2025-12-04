#ifndef MAGNOLIA_ARCH_M_ARCH_H
#define MAGNOLIA_ARCH_M_ARCH_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct m_arch_irq_handle m_arch_irq_handle_t;
typedef void (*m_arch_irq_handler_t)(void *arg);

void m_arch_start(void);

void m_arch_init_cores(void);
int m_arch_core_id(void);
int m_arch_core_count(void);

void m_arch_disable_interrupts(void);
void m_arch_enable_interrupts(void);
bool m_arch_is_irq_enabled(void);

void m_arch_halt(void);
void m_arch_reboot(void);
void m_arch_panic(const char *message);
void m_arch_shutdown(void);

void m_arch_sleep_ns(uint64_t ns);
uint64_t m_arch_get_time_ns(void);
void m_arch_timer_init(void);

void m_arch_yield(void);
void m_arch_switch_context(void *current, void *next);
void m_arch_task_init_stack(void *stack_top, size_t stack_size,
                            void (*entry)(void *), void *arg);

void m_arch_cache_flush(void *addr, size_t size);
void m_arch_cache_invalidate(void *addr, size_t size);
void m_arch_cache_barrier(void);

void m_arch_memory_barrier(void);
int32_t m_arch_atomic_add(volatile int32_t *ptr, int32_t value);
int32_t m_arch_atomic_cmpxchg(volatile int32_t *ptr,
                              int32_t expected,
                              int32_t desired);

void *m_arch_malloc(size_t size);
void m_arch_free(void *ptr);
size_t m_arch_get_free_memory(void);
size_t m_arch_get_total_memory(void);

void m_arch_idle(void);
void m_arch_wfi(void);
void m_arch_wfe(void);

void m_arch_dcache_enable(void);
void m_arch_dcache_disable(void);
void m_arch_icache_enable(void);
void m_arch_icache_disable(void);

uint32_t m_arch_get_entropy(void);

m_arch_irq_handle_t *m_arch_irq_attach_handler(int irq,
                                               m_arch_irq_handler_t handler,
                                               void *arg);
void m_arch_irq_detach_handler(m_arch_irq_handle_t *handle);
void m_arch_irq_ack(m_arch_irq_handle_t *handle);
void m_arch_irq_trigger(m_arch_irq_handle_t *handle);

void m_arch_clocks_init(void);
uint32_t m_arch_get_cpu_freq(void);
uint32_t m_arch_get_apb_freq(void);
uint32_t m_arch_get_xtal_freq(void);

void m_arch_stack_guard_enable(void);
void m_arch_stack_guard_disable(void);

void m_arch_fpu_enable(void);
void m_arch_fpu_disable(void);
void m_arch_fpu_save(void);
void m_arch_fpu_restore(void);

#ifdef __cplusplus
}
#endif

#endif /* MAGNOLIA_ARCH_M_ARCH_H */
