/*
 * Magnolia OS — IPC Subsystem
 * Purpose:
 *     Magnolia kernel monotonic timer utilities.
 * Description:
 *     Provide a consistent timebase and deadline helpers that other IPC
 *     primitives can rely on for timeout enforcement.
 *
 * © 2025 Magnolia Project
 */

#ifndef MAGNOLIA_TIMER_M_TIMER_H
#define MAGNOLIA_TIMER_M_TIMER_H

#include <stdint.h>
#include <stdbool.h>

#include "freertos/FreeRTOS.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Monotonic time expressed in microseconds.
 */
typedef uint64_t m_timer_time_t;

/**
 * @brief Special timeout value that represents an infinite wait.
 */
#define M_TIMER_TIMEOUT_FOREVER UINT64_MAX

/**
 * @brief Helper that describes when a deadline expires.
 */
typedef struct {
    m_timer_time_t target;
    bool infinite;
} m_timer_deadline_t;

/**
 * @brief Initialize the Magnolia timer subsystem.
 *
 * This is a no-op currently because the underlying ESP-IDF timer is ready by
 * default, but the function keeps the Magnolia timer API explicit for future
 * extensions.
 */
void m_timer_init(void);

/**
 * @brief Read the current Magnolia monotonic clock.
 *
 * @return Current time in microseconds.
 */
m_timer_time_t m_timer_get_monotonic(void);

/**
 * @brief Build a deadline relative to now.
 *
 * @param delta_us Time delta in microseconds (can be M_TIMER_TIMEOUT_FOREVER).
 * @return The resulting deadline object.
 */
m_timer_deadline_t m_timer_deadline_from_relative(uint64_t delta_us);

/**
 * @brief Convert a deadline to FreeRTOS ticks for blocking waits.
 *
 * @param deadline Pointer to the deadline to evaluate.
 * @return Tick count suitable for FreeRTOS waiting functions.
 */
TickType_t m_timer_deadline_to_ticks(const m_timer_deadline_t *deadline);

#ifdef __cplusplus
}
#endif

#endif /* MAGNOLIA_TIMER_M_TIMER_H */
