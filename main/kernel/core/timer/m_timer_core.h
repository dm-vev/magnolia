/**
 * @file kernel/core/timer/m_timer_core.h
 * @brief Core Magnolia timer types and initialization.
 * @details Defines the monotonic timebase that the scheduler and IPC layers
 *          rely on.
 */

#ifndef MAGNOLIA_TIMER_M_TIMER_CORE_H
#define MAGNOLIA_TIMER_M_TIMER_CORE_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Monotonic time expressed in microseconds.
 */
typedef uint64_t m_timer_time_t;

/**
 * @brief Special timeout value representing an infinite deadline.
 */
#define M_TIMER_TIMEOUT_FOREVER UINT64_MAX

/**
 * @brief Deadline descriptor used across Magnolia.
 */
typedef struct {
    m_timer_time_t target;
    bool infinite;
} m_timer_deadline_t;

/**
 * @brief Initialize the Magnolia timer subsystem.
 */
void m_timer_init(void);

/**
 * @brief Read the current Magnolia monotonic time.
 *
 * @return Monotonic time in microseconds.
 */
m_timer_time_t m_timer_get_monotonic(void);

#ifdef __cplusplus
}
#endif

#endif /* MAGNOLIA_TIMER_M_TIMER_CORE_H */
