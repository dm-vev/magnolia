/*
 * Magnolia OS — Job Context Public API
 * Purpose:
 *     Share job field identifiers, policies, and userland entrypoints.
 *
 * © 2025 Magnolia Project
 */

#ifndef MAGNOLIA_JOB_JCTX_PUBLIC_H
#define MAGNOLIA_JOB_JCTX_PUBLIC_H

#include <stddef.h>
#include <stdint.h>

#include "sdkconfig.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifndef MAGNOLIA_JOB_M_JOB_ID_T_DEFINED
#define MAGNOLIA_JOB_M_JOB_ID_T_DEFINED
typedef struct m_job_handle m_job_handle_t;
typedef m_job_handle_t *m_job_id_t;
#endif

#define JOB_CTX_CWD_MAX_LEN CONFIG_MAGNOLIA_JOB_CTX_CWD_MAX_LEN
#define JOB_CTX_ATTR_KEY_MAX_LEN CONFIG_MAGNOLIA_JOB_CTX_ATTR_KEY_MAX_LEN
#define JOB_CTX_ATTR_VALUE_MAX_LEN CONFIG_MAGNOLIA_JOB_CTX_ATTR_VALUE_MAX_LEN
#define JOB_CTX_USER_ATTR_MAX CONFIG_MAGNOLIA_JOB_CTX_USER_ATTR_MAX
#define JOB_CTX_TLS_SLOT_COUNT CONFIG_MAGNOLIA_JOB_CTX_TLS_SLOT_COUNT

typedef enum {
    JOB_CTX_OK = 0,
    JOB_CTX_ERR_INVALID_PARAM,
    JOB_CTX_ERR_INVALID_FIELD,
    JOB_CTX_ERR_BUFFER_TOO_SMALL,
    JOB_CTX_ERR_NO_PERMISSION,
    JOB_CTX_ERR_NOT_READY,
} job_ctx_error_t;

typedef enum {
    JOB_CTX_FIELD_POLICY_PUBLIC = 0,
    JOB_CTX_FIELD_POLICY_PROTECTED,
    JOB_CTX_FIELD_POLICY_PRIVATE,
} job_ctx_field_policy_t;

typedef enum {
    JOB_CTX_FIELD_TYPE_RAW = 0,
    JOB_CTX_FIELD_TYPE_STRING,
} job_ctx_field_type_t;

typedef enum {
    JOB_CTX_SCHED_STATE_PENDING = 0,
    JOB_CTX_SCHED_STATE_RUNNING,
    JOB_CTX_SCHED_STATE_COMPLETED,
    JOB_CTX_SCHED_STATE_CANCELED,
} job_ctx_scheduler_state_t;

typedef void (*job_ctx_tls_destructor_t)(void *value);

typedef struct job_ctx job_ctx_t;

typedef enum {
    JOB_CTX_FIELD_JOB_ID = 0,
    JOB_CTX_FIELD_PARENT_JOB_ID,
    JOB_CTX_FIELD_UID,
    JOB_CTX_FIELD_GID,
    JOB_CTX_FIELD_EUID,
    JOB_CTX_FIELD_EGID,
    JOB_CTX_FIELD_CWD,
    JOB_CTX_FIELD_TRACE_ID,
    JOB_CTX_FIELD_SUBMITTED_AT,
    JOB_CTX_FIELD_STARTED_AT,
    JOB_CTX_FIELD_COMPLETED_AT,
    JOB_CTX_FIELD_DEADLINE,
    JOB_CTX_FIELD_PRIORITY_HINT,
    JOB_CTX_FIELD_USER_ATTR_KEY_0,
    JOB_CTX_FIELD_USER_ATTR_VALUE_0,
    JOB_CTX_FIELD_USER_ATTR_KEY_1,
    JOB_CTX_FIELD_USER_ATTR_VALUE_1,
    JOB_CTX_FIELD_USER_ATTR_KEY_2,
    JOB_CTX_FIELD_USER_ATTR_VALUE_2,
    JOB_CTX_FIELD_USER_ATTR_KEY_3,
    JOB_CTX_FIELD_USER_ATTR_VALUE_3,
    JOB_CTX_FIELD_INTERNAL_CANCELLED,
    JOB_CTX_FIELD_INTERNAL_SCHED_STATE,
    JOB_CTX_FIELD_INTERNAL_REFCOUNT,
    JOB_CTX_FIELD_TLS_SLOT_VALUE_0,
    JOB_CTX_FIELD_TLS_SLOT_VALUE_1,
    JOB_CTX_FIELD_TLS_SLOT_VALUE_2,
    JOB_CTX_FIELD_TLS_SLOT_VALUE_3,
    JOB_CTX_FIELD_TLS_SLOT_DESTRUCTOR_0,
    JOB_CTX_FIELD_TLS_SLOT_DESTRUCTOR_1,
    JOB_CTX_FIELD_TLS_SLOT_DESTRUCTOR_2,
    JOB_CTX_FIELD_TLS_SLOT_DESTRUCTOR_3,
    JOB_CTX_FIELD_COUNT,
} job_ctx_field_id_t;

#define JOB_CTX_ATTR_KEY_FIELD(index) (JOB_CTX_FIELD_USER_ATTR_KEY_0 + ((index) * 2))
#define JOB_CTX_ATTR_VALUE_FIELD(index) (JOB_CTX_FIELD_USER_ATTR_VALUE_0 + ((index) * 2))
#define JOB_CTX_TLS_VALUE_FIELD(index) (JOB_CTX_FIELD_TLS_SLOT_VALUE_0 + (index))
#define JOB_CTX_TLS_DESTRUCTOR_FIELD(index) (JOB_CTX_FIELD_TLS_SLOT_DESTRUCTOR_0 + (index))

job_ctx_error_t jctx_field_get_user(m_job_id_t job,
                                    job_ctx_field_id_t field,
                                    void *out_buf,
                                    size_t buf_size);

job_ctx_error_t jctx_field_set_user(m_job_id_t job,
                                    job_ctx_field_id_t field,
                                    const void *value,
                                    size_t value_size);

job_ctx_error_t m_job_field_get(m_job_id_t job,
                                job_ctx_field_id_t field,
                                void *out_buf,
                                size_t buf_size);

job_ctx_error_t m_job_field_set(m_job_id_t job,
                                job_ctx_field_id_t field,
                                const void *value,
                                size_t value_size);

#if CONFIG_MAGNOLIA_JOB_CTX_FIELD_COUNT != JOB_CTX_FIELD_COUNT
#error "CONFIG_MAGNOLIA_JOB_CTX_FIELD_COUNT must match the built-in field table"
#endif

#ifdef __cplusplus
}
#endif

#endif /* MAGNOLIA_JOB_JCTX_PUBLIC_H */
