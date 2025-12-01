/*
 * Magnolia OS — IPC Subsystem
 * Purpose:
 *     Magnolia timer utilities for Magnolia kernel abstractions.
 * Description:
 *     Wrap the ESP-IDF monotonic clock with Magnolia-friendly deadlines and
 *     conversions so that IPC timeouts stay consistent.
 *
 * © 2025 Magnolia Project
 */

#include "kernel/core/timer/m_timer.h"

#include "esp_timer.h"

/**
 * @brief Initialize the Magnolia timer subsystem.
 *
 * This implementation currently relies on the default ESP-IDF timer state.
 */
void m_timer_init(void)
{
    /* Nothing to initialize for now. */
}

/**
 * @brief Retrieve the current Magnolia monotonic time.
 *
 * @return Monotonic time in microseconds.
 */

m_timer_time_t m_timer_get_monotonic(void)
{
    return (m_timer_time_t)esp_timer_get_time();
}

/**
 * @brief Build a deadline from a relative delta.
 *
 * @param delta_us Time delta expressed in microseconds. Use
 *                 M_TIMER_TIMEOUT_FOREVER for an infinite deadline.
 * @return Deadline object representing the target timestamp.
 */

m_timer_deadline_t m_timer_deadline_from_relative(uint64_t delta_us)
{
    if (delta_us == M_TIMER_TIMEOUT_FOREVER) {
        return (m_timer_deadline_t){
            .target = 0,
            .infinite = true,
        };
    }

    return (m_timer_deadline_t){
        .target = m_timer_get_monotonic() + delta_us,
        .infinite = false,
    };
}

/**
 * @brief Convert a deadline into FreeRTOS ticks for blocking waits.
 *
 * @param deadline Deadline to evaluate.
 * @return Tick count or portMAX_DELAY for infinite deadlines.
 */

TickType_t m_timer_deadline_to_ticks(const m_timer_deadline_t *deadline)
{
    if (deadline == NULL || deadline->infinite) {
        return portMAX_DELAY;
    }

    m_timer_time_t now = m_timer_get_monotonic();
    if (deadline->target <= now) {
        return 0;
    }

    uint64_t remaining_us = deadline->target - now;
    uint64_t ticks = (remaining_us + 999ULL) / 1000ULL;

    if (ticks >= portMAX_DELAY) {
        return portMAX_DELAY - 1;
    }

    return (TickType_t)ticks;
}
