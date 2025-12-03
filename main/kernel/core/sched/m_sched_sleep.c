/**
 * @file kernel/core/sched/m_sched_sleep.c
 * @brief Scheduler sleep helpers.
 * @details Builds on top of the wait context to offer deterministic delays
 *          while delegating timer work to the deadline utility.
 */

#include "kernel/core/sched/m_sched_sleep.h"

/**
 * @brief Sleep for the requested number of milliseconds.
 */
m_sched_wait_result_t m_sched_sleep_ms(uint32_t milliseconds)
{
    m_sched_wait_context_t context = {0};
    m_sched_wait_context_prepare_with_reason(&context,
                                            M_SCHED_WAIT_REASON_DELAY);
    uint64_t microseconds = (uint64_t)milliseconds * 1000ULL;
    m_timer_deadline_t deadline = m_timer_deadline_from_relative(microseconds);
    return m_sched_wait_block(&context, &deadline);
}

/**
 * @brief Sleep until a monotonic deadline expires.
 */
m_sched_wait_result_t m_sched_sleep_until(m_timer_time_t deadline)
{
    m_sched_wait_context_t context = {0};
    m_sched_wait_context_prepare_with_reason(&context,
                                            M_SCHED_WAIT_REASON_DELAY);
    m_timer_deadline_t target = {.target = deadline,
                                 .infinite = (deadline ==
                                              M_TIMER_TIMEOUT_FOREVER)};
    return m_sched_wait_block(&context, &target);
}
