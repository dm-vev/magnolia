/**
 * @file        m_job_core.h
 * @brief       Core job lifecycle and metadata declarations.
 * @details     Defines the job handle, error codes, and lifecycle helpers that other
 *              job subsystem components rely on.
 */
#ifndef MAGNOLIA_JOB_M_JOB_CORE_H
#define MAGNOLIA_JOB_M_JOB_CORE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"
#include "kernel/core/ipc/ipc_scheduler_bridge.h"
#include "kernel/core/job/jctx.h"
#include "kernel/core/timer/m_timer.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief   Opaque job handle used throughout the subsystem.
 */
typedef struct m_job_handle m_job_handle_t;
typedef m_job_handle_t *m_job_id_t;

/**
 * @brief   Result status produced by job handlers.
 */
typedef enum {
    M_JOB_RESULT_SUCCESS = 0,
    M_JOB_RESULT_ERROR,
    M_JOB_RESULT_CANCELLED,
} m_job_result_status_t;

/**
 * @brief   Descriptor describing a handler output payload.
 */
typedef struct {
    m_job_result_status_t status;
    const void *payload;
    size_t payload_size;
} m_job_result_descriptor_t;

/**
 * @brief   Alias describing the return type of job handlers.
 */
typedef m_job_result_descriptor_t m_job_handler_result_t;

/**
 * @brief   Job handler callback invoked by worker threads.
 */
typedef m_job_handler_result_t (*m_job_handler_t)(m_job_id_t job, void *data);

/**
 * @brief   Error codes returned by job subsystem calls.
 */
typedef enum {
    M_JOB_OK = 0,
    M_JOB_ERR_INVALID_PARAM,
    M_JOB_ERR_INVALID_HANDLE,
    M_JOB_ERR_NO_MEMORY,
    M_JOB_ERR_QUEUE_FULL,
    M_JOB_ERR_TIMEOUT,
    M_JOB_ERR_DESTROYED,
    M_JOB_ERR_STATE,
    M_JOB_ERR_SHUTDOWN,
    M_JOB_ERR_NOT_READY,
    M_JOB_ERR_BUSY,
} m_job_error_t;

/**
 * @brief   Internal state progression tracked by a job handle.
 */
typedef enum {
    M_JOB_STATE_PENDING,
    M_JOB_STATE_RUNNING,
    M_JOB_STATE_COMPLETED,
} m_job_state_t;

/**
 * @brief   Internal job handle definition shared across modules.
 */
struct m_job_handle {
    m_job_handler_t handler;
    void *data;
    job_ctx_t *ctx;
    m_job_state_t state;
    bool cancelled;
    bool destroyed;
    bool result_ready;
    m_job_result_descriptor_t result;
    size_t future_count;
    size_t waiter_count;
    portMUX_TYPE lock;
    ipc_wait_queue_t waiters;
};

/**
 * @brief   Cancel a pending job and record a cancellation result.
 *
 * @param   job Job handle returned during submission.
 *
 * @return  M_JOB_OK             Cancellation request accepted.
 * @return  M_JOB_ERR_INVALID_HANDLE Job handle pointer was NULL.
 * @return  M_JOB_ERR_STATE      Job already completed or destroyed.
 */
m_job_error_t m_job_cancel(m_job_id_t job);

/**
 * @brief   Destroy a job handle once its result is observed and no futures remain.
 *
 * @param   job Job handle returned during submission.
 *
 * @return  M_JOB_OK             Success.
 * @return  M_JOB_ERR_INVALID_HANDLE Job handle pointer was NULL.
 * @return  M_JOB_ERR_DESTROYED  Job handle already destroyed.
 * @return  M_JOB_ERR_NOT_READY  Job result is not ready yet.
 * @return  M_JOB_ERR_BUSY       Job has attached futures preventing destruction.
 */
m_job_error_t m_job_handle_destroy(m_job_id_t job);

/**
 * @brief   Retrieve a scheduler-visible job context field.
 */
job_ctx_error_t m_job_field_get(m_job_id_t job,
                                job_ctx_field_id_t field,
                                void *out_buf,
                                size_t buf_size);

/**
 * @brief   Update a scheduler-visible job context field.
 */
job_ctx_error_t m_job_field_set(m_job_id_t job,
                                job_ctx_field_id_t field,
                                const void *value,
                                size_t value_size);

/**
 * @brief   Allocate a new job handle ready for submission.
 */
m_job_handle_t *_m_job_handle_create(m_job_handler_t handler,
                                      void *data,
                                      m_job_id_t parent_job);

/**
 * @brief   Record that a job handler completed with the provided result.
 */
void _m_job_handle_set_result(m_job_handle_t *handle,
                              m_job_handler_result_t result);

/**
 * @brief   Record a cancellation outcome for the supplied handle.
 */
void _m_job_handle_record_cancellation(m_job_handle_t *handle);

#ifdef __cplusplus
}
#endif

#endif /* MAGNOLIA_JOB_M_JOB_CORE_H */
