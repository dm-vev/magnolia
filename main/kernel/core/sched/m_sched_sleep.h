/**
 * @file kernel/core/sched/m_sched_sleep.h
 * @brief Sleep helpers built on top of Magnolia wait contexts.
 * @details Provides monotonic delay APIs and exposes deadline integration
 *          without touching task creation or wake logic.
 */

#ifndef MAGNOLIA_SCHED_M_SCHED_SLEEP_H
#define MAGNOLIA_SCHED_M_SCHED_SLEEP_H

#include "kernel/core/sched/m_sched_wait.h"
#include "kernel/core/timer/m_timer.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Sleep for the given number of milliseconds.
 *
 * @param milliseconds Delay duration.
 * @return Result of the underlying wait.
 */
m_sched_wait_result_t m_sched_sleep_ms(uint32_t milliseconds);

/**
 * @brief Sleep until the provided Magnolia monotonic deadline.
 *
 * @param deadline Absolute deadline.
 * @return Result of the underlying wait.
 */
m_sched_wait_result_t m_sched_sleep_until(m_timer_time_t deadline);

#ifdef __cplusplus
}
#endif

#endif /* MAGNOLIA_SCHED_M_SCHED_SLEEP_H */
