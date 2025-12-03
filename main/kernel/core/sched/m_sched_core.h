/**
 * @file kernel/core/sched/m_sched_core.h
 * @brief Core scheduler primitives for Magnolia SAL.
 * @details Defines the task metadata, registry primitives, and creation APIs
 *          that are shared between the scheduler, wait, and diagnostics
 *          layers.
 */

#ifndef MAGNOLIA_SCHED_M_SCHED_CORE_H
#define MAGNOLIA_SCHED_M_SCHED_CORE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#define M_SCHED_TASK_TAG_MAX_LEN 32
#define M_SCHED_CPU_AFFINITY_ANY (-1)
#define M_SCHED_TASK_ID_INVALID 0
#define M_SCHED_TASK_FLAG_NONE 0u
#define M_SCHED_TASK_FLAG_WORKER (1u << 0)

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Handle that uniquely identifies a Magnolia task.
 */
typedef uint32_t m_sched_task_id_t;

/**
 * @brief General scheduler error codes.
 */
typedef enum {
    M_SCHED_OK = 0,
    M_SCHED_ERR_INVALID_PARAM,
    M_SCHED_ERR_NO_MEMORY,
    M_SCHED_ERR_NOT_FOUND,
    M_SCHED_ERR_STATE,
    M_SCHED_ERR_SHUTDOWN,
} m_sched_error_t;

/**
 * @brief Scheduler-visible task states.
 */
typedef enum {
    M_SCHED_STATE_READY,
    M_SCHED_STATE_RUNNING,
    M_SCHED_STATE_WAITING,
    M_SCHED_STATE_SUSPENDED,
    M_SCHED_STATE_TERMINATED,
} m_sched_task_state_t;

/**
 * @brief Describes why a task is blocked at the scheduler level.
 */
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

/**
 * @brief Result of blocking waits handled by Magnolia.
 */
typedef enum {
    M_SCHED_WAIT_RESULT_OK = 0,
    M_SCHED_WAIT_RESULT_TIMEOUT,
    M_SCHED_WAIT_RESULT_OBJECT_DESTROYED,
    M_SCHED_WAIT_RESULT_SHUTDOWN,
    M_SCHED_WAIT_RESULT_ABORTED,
} m_sched_wait_result_t;

typedef struct m_sched_task_metadata m_sched_task_metadata_t;

/**
 * @brief Per-task metadata that is shared inside the scheduler registry.
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
 * @brief Options used when spawning a Magnolia task.
 */
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

/**
 * @brief Initialize the Magnolia scheduler registry.
 */
void m_sched_init(void);

/**
 * @brief Spawn a Magnolia-managed task.
 *
 * @param options Task creation options.
 * @param out_id Assigned task identifier.
 * @return Scheduler error status.
 */
m_sched_error_t m_sched_task_create(const m_sched_task_options_t *options,
                                    m_sched_task_id_t *out_id);

/**
 * @brief Terminate a Magnolia task by id.
 *
 * @param id Task identifier.
 * @return Scheduler error status.
 */
m_sched_error_t m_sched_task_destroy(m_sched_task_id_t id);

/**
 * @brief Suspend a Magnolia task by id.
 *
 * @param id Task identifier.
 * @return Scheduler error status.
 */
m_sched_error_t m_sched_task_suspend(m_sched_task_id_t id);

/**
 * @brief Resume a Magnolia task by id.
 *
 * @param id Task identifier.
 * @return Scheduler error status.
 */
m_sched_error_t m_sched_task_resume(m_sched_task_id_t id);

/**
 * @brief Yield the current Magnolia task to the scheduler.
 */
void m_sched_task_yield(void);

#ifdef __cplusplus
}
#endif

#endif /* MAGNOLIA_SCHED_M_SCHED_CORE_H */
