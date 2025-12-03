/**
 * @file kernel/core/timer/m_timer_deadline.c
 * @brief Deadline computations for Magnolia timers.
 * @details Converts between microseconds and ticks, handles infinite deadlines,
 *          and protects against wrap-around.
 */

#include "kernel/core/timer/m_timer_deadline.h"

m_timer_deadline_t m_timer_deadline_from_relative(uint64_t delta_us)
{
    if (delta_us == M_TIMER_TIMEOUT_FOREVER) {
        return (m_timer_deadline_t){.target = 0, .infinite = true};
    }

    return (m_timer_deadline_t){
        .target = m_timer_get_monotonic() + delta_us,
        .infinite = false,
    };
}

TickType_t m_timer_delta_to_ticks(uint64_t delta_us)
{
    if (delta_us == M_TIMER_TIMEOUT_FOREVER) {
        return portMAX_DELAY;
    }

    uint64_t remaining_ms = (delta_us + 999ULL) / 1000ULL;
    uint64_t tick_ms = portTICK_PERIOD_MS;
    uint64_t ticks = (remaining_ms + tick_ms - 1ULL) / tick_ms;

    if (ticks == 0ULL) {
        ticks = 1ULL;
    }

    if (ticks >= portMAX_DELAY) {
        return portMAX_DELAY - 1;
    }

    return (TickType_t)ticks;
}

TickType_t m_timer_deadline_to_ticks(const m_timer_deadline_t *deadline)
{
    if (deadline == NULL || deadline->infinite) {
        return portMAX_DELAY;
    }

    m_timer_time_t now = m_timer_get_monotonic();
    uint64_t remaining = m_timer_deadline_delta_us(deadline, now);
    return m_timer_delta_to_ticks(remaining);
}

uint64_t m_timer_ticks_to_us(TickType_t ticks)
{
    uint64_t milliseconds = (uint64_t)ticks * portTICK_PERIOD_MS;
    return milliseconds * 1000ULL;
}

uint64_t m_timer_deadline_delta_us(const m_timer_deadline_t *deadline,
                                   m_timer_time_t reference)
{
    if (deadline == NULL || deadline->infinite) {
        return UINT64_MAX;
    }

    if (deadline->target <= reference) {
        return 0;
    }

    return deadline->target - reference;
}
