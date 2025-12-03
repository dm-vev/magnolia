/**
 * @file kernel/core/timer/m_timer_deadline.h
 * @brief Deadline helper functions for Magnolia timers.
 * @details Handles conversions between microseconds and ticks, relative
 *          adjustments, and overflow-safe delta calculations.
 */

#ifndef MAGNOLIA_TIMER_M_TIMER_DEADLINE_H
#define MAGNOLIA_TIMER_M_TIMER_DEADLINE_H

#include <stdint.h>

#include "freertos/FreeRTOS.h"

#include "kernel/core/timer/m_timer_core.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Build a deadline relative to now.
 *
 * @param delta_us Relative delay in microseconds.
 * @return Epoch deadline.
 */
m_timer_deadline_t m_timer_deadline_from_relative(uint64_t delta_us);

/**
 * @brief Convert a deadline to FreeRTOS ticks.
 *
 * @param deadline Deadline to evaluate.
 * @return Tick count.
 */
TickType_t m_timer_deadline_to_ticks(const m_timer_deadline_t *deadline);

/**
 * @brief Convert a relative delta to FreeRTOS ticks.
 *
 * @param delta_us Relative delay in microseconds.
 * @return Tick count suitable for FreeRTOS waits.
 */
TickType_t m_timer_delta_to_ticks(uint64_t delta_us);

/**
 * @brief Convert FreeRTOS ticks back to microseconds.
 *
 * @param ticks Tick count.
 * @return Equivalent microseconds.
 */
uint64_t m_timer_ticks_to_us(TickType_t ticks);

/**
 * @brief Compute remaining microseconds until @p deadline from @p reference.
 *
 * @param deadline Deadline descriptor.
 * @param reference Monotonic reference time.
 * @return Remaining microseconds or UINT64_MAX for infinite deadlines.
 */
uint64_t m_timer_deadline_delta_us(const m_timer_deadline_t *deadline,
                                   m_timer_time_t reference);

#ifdef __cplusplus
}
#endif

#endif /* MAGNOLIA_TIMER_M_TIMER_DEADLINE_H */
