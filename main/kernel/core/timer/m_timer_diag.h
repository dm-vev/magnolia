/**
 * @file kernel/core/timer/m_timer_diag.h
 * @brief Timer diagnostics helpers.
 * @details Reports current time, pending deadlines, and queue depth for tracing
 *          and debugging.
 */

#ifndef MAGNOLIA_TIMER_M_TIMER_DIAG_H
#define MAGNOLIA_TIMER_M_TIMER_DIAG_H

#include <stdbool.h>
#include <stddef.h>

#include "kernel/core/timer/m_timer_deadline.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Snapshot of the Magnolia timer state.
 */
typedef struct {
    m_timer_time_t now;
    size_t queue_depth;
    bool has_next;
    m_timer_deadline_t next_deadline;
    uint64_t next_delta_us;
} m_timer_diag_report_t;

/**
 * @brief Populate the diagnostics report with live data.
 *
 * @param report Output buffer.
 */
void m_timer_diag_snapshot(m_timer_diag_report_t *report);

#ifdef __cplusplus
}
#endif

#endif /* MAGNOLIA_TIMER_M_TIMER_DIAG_H */
