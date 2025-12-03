#include "kernel/arch/m_arch.h"

#include <stdint.h>

#include "sdkconfig.h"

#include "esp_private/esp_clk.h"
#include "esp_random.h"
#include "esp_rom_sys.h"
#include "esp_sleep.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_private/hw_stack_guard.h"

void m_arch_sleep_ns(uint64_t ns)
{
    uint64_t us = ns / 1000ull;
    if (ns % 1000ull) {
        us += 1;
    }
    if (us == 0) {
        us = 1;
    }

    esp_rom_delay_us(us);
}

uint64_t m_arch_get_time_ns(void)
{
    return (uint64_t)esp_timer_get_time() * 1000ull;
}

void m_arch_timer_init(void)
{
    (void)esp_timer_init();
}

void m_arch_clocks_init(void) {}

uint32_t m_arch_get_cpu_freq(void)
{
    return esp_clk_cpu_freq();
}

uint32_t m_arch_get_apb_freq(void)
{
    return esp_clk_apb_freq();
}

uint32_t m_arch_get_xtal_freq(void)
{
    return esp_clk_xtal_freq();
}

uint32_t m_arch_get_entropy(void)
{
    return esp_random();
}

void m_arch_reboot(void)
{
    esp_restart();
}

void m_arch_shutdown(void)
{
    esp_deep_sleep_start();
}

void m_arch_stack_guard_enable(void)
{
    esp_hw_stack_guard_monitor_start();
}

void m_arch_stack_guard_disable(void)
{
    esp_hw_stack_guard_monitor_stop();
}

void m_arch_fpu_enable(void) {}

void m_arch_fpu_disable(void) {}

void m_arch_fpu_save(void) {}

void m_arch_fpu_restore(void) {}
