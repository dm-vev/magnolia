/**
 * @file kernel/core/sched/m_sched.h
 * @brief Magnolia scheduler public API facade.
 * @details Aggregates the core, wait, sleep, worker, and diagnostics headers
 *          so consumers only need a single include.
 */

#ifndef MAGNOLIA_SCHED_M_SCHED_H
#define MAGNOLIA_SCHED_M_SCHED_H

#include "kernel/core/sched/m_sched_core.h"
#include "kernel/core/sched/m_sched_wait.h"
#include "kernel/core/sched/m_sched_sleep.h"
#include "kernel/core/sched/m_sched_worker.h"
#include "kernel/core/sched/m_sched_diag.h"

#endif /* MAGNOLIA_SCHED_M_SCHED_H */
