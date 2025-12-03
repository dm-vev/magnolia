/**
 * @file kernel/core/timer/m_timer_core.c
 * @brief Magnolia monotonic timer implementation.
 * @details Wraps the ESP-IDF monotonic clock and wires up the Magnolia timer
 *          queue.
 */

#include "kernel/core/timer/m_timer_core.h"
#include "kernel/core/timer/m_timer_queue.h"

#include "esp_timer.h"

void m_timer_init(void)
{
    m_timer_queue_init();
}

m_timer_time_t m_timer_get_monotonic(void)
{
    return (m_timer_time_t)esp_timer_get_time();
}
