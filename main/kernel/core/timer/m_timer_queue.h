/**
 * @file kernel/core/timer/m_timer_queue.h
 * @brief Magnolia timer queue interface.
 * @details Maintains an ordered timer event list that can be used by future
 *          subsystems without mixing with the core deadline logic.
 */

#ifndef MAGNOLIA_TIMER_M_TIMER_QUEUE_H
#define MAGNOLIA_TIMER_M_TIMER_QUEUE_H

#include <stdbool.h>
#include <stddef.h>

#include "kernel/core/timer/m_timer_deadline.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct m_timer_queue_entry m_timer_queue_entry_t;

typedef void (*m_timer_queue_callback_t)(m_timer_queue_entry_t *entry,
                                         void *context);

/**
 * @brief Initialize the internal queue state.
 */
void m_timer_queue_init(void);

/**
 * @brief Schedule a deadline into the queue.
 *
 * @param deadline Deadline descriptor.
 * @param callback Callback invoked when the deadline expires.
 * @param context Arbitrary user context.
 * @return Entry handle that can be used for cancellation.
 */
m_timer_queue_entry_t *m_timer_queue_schedule(
        m_timer_deadline_t deadline,
        m_timer_queue_callback_t callback,
        void *context);

/**
 * @brief Cancel a scheduled entry.
 *
 * @param entry Entry returned by @p m_timer_queue_schedule.
 * @return true if the entry was removed.
 */
bool m_timer_queue_cancel(m_timer_queue_entry_t *entry);

/**
 * @brief Dispatch all expirations up to @p now.
 */
void m_timer_queue_process(m_timer_time_t now);

/**
 * @brief Return the number of pending entries.
 */
size_t m_timer_queue_length(void);

/**
 * @brief Peek the next deadline without removing it.
 *
 * @param out Deadline output location.
 * @return true if an entry exists.
 */
bool m_timer_queue_next_deadline(m_timer_deadline_t *out);

#ifdef __cplusplus
}
#endif

#endif /* MAGNOLIA_TIMER_M_TIMER_QUEUE_H */
