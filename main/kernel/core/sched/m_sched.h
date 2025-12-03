/*
 * Magnolia OS — Scheduler Subsystem
 * Purpose:
 *     Magnolia scheduler abstraction layer.
 * Description:
 *     Provide a scheduler-neutral API on top of FreeRTOS for task management,
 *     wait/wake helpers, diagnostics, and worker lifecycle hooks.
 *
 * © 2025 Magnolia Project
 */

#ifndef MAGNOLIA_SCHED_M_SCHED_H
#define MAGNOLIA_SCHED_M_SCHED_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "kernel/core/timer/m_timer.h"

#define M_SCHED_TASK_TAG_MAX_LEN 32
#define M_SCHED_CPU_AFFINITY_ANY (-1)
#define M_SCHED_TASK_ID_INVALID 0

#ifdef __cplusplus
extern "C" {
#endif

typedef uint32_t m_sched_task_id_t;

typedef enum {
    M_SCHED_OK = 0,
    M_SCHED_ERR_INVALID_PARAM,
    M_SCHED_ERR_NO_MEMORY,
    M_SCHED_ERR_NOT_FOUND,
    M_SCHED_ERR_STATE,
    M_SCHED_ERR_SHUTDOWN,
} m_sched_error_t;

typedef enum {
    M_SCHED_STATE_READY,
    M_SCHED_STATE_RUNNING,
    M_SCHED_STATE_WAITING,
    M_SCHED_STATE_SUSPENDED,
    M_SCHED_STATE_TERMINATED,
} m_sched_task_state_t;

typedef enum {
    M_SCHED_WAIT_REASON_NONE,
    M_SCHED_WAIT_REASON_IPC,
    M_SCHED_WAIT_REASON_DELAY,
    M_SCHED_WAIT_REASON_EVENT,
    M_SCHED_WAIT_REASON_EVENT_FLAGS,
    M_SCHED_WAIT_REASON_JOB,
    M_SCHED_WAIT_REASON_SHM_READ,
    M_SCHED_WAIT_REASON_SHM_WRITE,
} m_sched_wait_reason_t;

typedef enum {
    M_SCHED_WAIT_RESULT_OK = 0,
    M_SCHED_WAIT_RESULT_TIMEOUT,
    M_SCHED_WAIT_RESULT_OBJECT_DESTROYED,
    M_SCHED_WAIT_RESULT_SHUTDOWN,
    M_SCHED_WAIT_RESULT_ABORTED,
} m_sched_wait_result_t;

typedef struct m_sched_task_metadata m_sched_task_metadata_t;

#define M_SCHED_TASK_FLAG_NONE 0u
#define M_SCHED_TASK_FLAG_WORKER (1u << 0)

typedef struct {
    const char *name;
    TaskFunction_t entry;
    void *argument;
    size_t stack_depth;
    UBaseType_t priority;
    int cpu_affinity;
    uint32_t creation_flags;
    const char *tag;
    void *user_data;
} m_sched_task_options_t;

typedef void (*m_sched_worker_lifecycle_hook_fn)(m_sched_task_id_t task_id,
                                                 m_sched_task_metadata_t *metadata,
                                                 void *user_data);

typedef struct {
    m_sched_worker_lifecycle_hook_fn on_worker_start;
    m_sched_worker_lifecycle_hook_fn on_worker_stop;
    void *user_data;
} m_sched_worker_hooks_t;

typedef struct {
    m_sched_task_id_t id;
    char name[configMAX_TASK_NAME_LEN];
    m_sched_task_state_t state;
    m_sched_wait_reason_t wait_reason;
    char tag[M_SCHED_TASK_TAG_MAX_LEN];
} m_sched_task_diag_entry_t;

/**
 * @brief Magnolia per-task context.
 */
struct m_sched_task_metadata {
    m_sched_task_id_t id;
    TaskHandle_t handle;
    m_sched_task_state_t state;
    m_sched_wait_reason_t wait_reason;
    uint32_t creation_flags;
    int cpu_affinity;
    char name[configMAX_TASK_NAME_LEN];
    char tag[M_SCHED_TASK_TAG_MAX_LEN];
    void *user_data;
    bool finalized;
    m_sched_task_metadata_t *next;
};

/**
 * @brief Per-wait context that Magnolia IPC uses while blocked.
 */
typedef struct {
    SemaphoreHandle_t semaphore;
    StaticSemaphore_t storage;
    TaskHandle_t task;
    m_sched_task_metadata_t *owner;
    m_sched_wait_reason_t reason;
    m_sched_wait_result_t result;
    bool armed;
    bool initialized;
} m_sched_wait_context_t;

void m_sched_init(void);

m_sched_error_t m_sched_task_create(const m_sched_task_options_t *options,
                                    m_sched_task_id_t *out_id);
m_sched_error_t m_sched_task_destroy(m_sched_task_id_t id);
m_sched_error_t m_sched_task_suspend(m_sched_task_id_t id);
m_sched_error_t m_sched_task_resume(m_sched_task_id_t id);
void m_sched_task_yield(void);

m_sched_wait_result_t m_sched_sleep_ms(uint32_t milliseconds);
m_sched_wait_result_t m_sched_sleep_until(m_timer_time_t deadline);

void m_sched_worker_hooks_register(const m_sched_worker_hooks_t *hooks);

void m_sched_wait_context_prepare(m_sched_wait_context_t *ctx);
void m_sched_wait_context_prepare_with_reason(m_sched_wait_context_t *ctx,
                                               m_sched_wait_reason_t reason);
m_sched_wait_result_t m_sched_wait_block(m_sched_wait_context_t *ctx,
                                          const m_timer_deadline_t *deadline);
void m_sched_wait_wake(m_sched_wait_context_t *ctx,
                       m_sched_wait_result_t result);

size_t m_sched_task_snapshot(m_sched_task_diag_entry_t *buffer, size_t capacity);
bool m_sched_task_metadata_get(m_sched_task_id_t id,
                               m_sched_task_metadata_t *out);
bool m_sched_task_id_is_valid(m_sched_task_id_t id);

#ifdef __cplusplus
}
#endif

#endif /* MAGNOLIA_SCHED_M_SCHED_H */
