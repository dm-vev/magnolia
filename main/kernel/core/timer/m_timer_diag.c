/**
 * @file kernel/core/timer/m_timer_diag.c
 * @brief Timer diagnostics implementation.
 * @details Collects queue and deadline data for tracing and testing helpers.
 */

#include "kernel/core/timer/m_timer_diag.h"
#include "kernel/core/timer/m_timer_queue.h"

/**
 * @brief Populate a diagnostics snapshot.
 */
void m_timer_diag_snapshot(m_timer_diag_report_t *report)
{
    if (report == NULL) {
        return;
    }

    report->now = m_timer_get_monotonic();
    report->queue_depth = m_timer_queue_length();
    report->has_next = m_timer_queue_next_deadline(&report->next_deadline);
    if (report->has_next) {
        report->next_delta_us = m_timer_deadline_delta_us(
                &report->next_deadline, report->now);
    } else {
        report->next_delta_us = 0;
    }
}
