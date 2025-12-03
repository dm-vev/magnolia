#ifndef MAGNOLIA_JOB_JCTX_H
#define MAGNOLIA_JOB_JCTX_H

#include <stdbool.h>
#include "kernel/core/job/jctx_public.h"
#include "kernel/core/timer/m_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"
#include "freertos/task.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    char key[JOB_CTX_ATTR_KEY_MAX_LEN];
    char value[JOB_CTX_ATTR_VALUE_MAX_LEN];
} job_ctx_user_attribute_t;

typedef struct {
    bool cancelled;
    job_ctx_scheduler_state_t scheduler_state;
    size_t refcount;
} job_ctx_internal_flags_t;

typedef struct {
    void *slots[JOB_CTX_TLS_SLOT_COUNT];
    job_ctx_tls_destructor_t destructors[JOB_CTX_TLS_SLOT_COUNT];
} job_ctx_tls_t;

struct job_ctx {
    m_job_id_t job_id;
    m_job_id_t parent_job_id;
    uint32_t uid;
    uint32_t gid;
    uint32_t euid;
    uint32_t egid;
    char cwd[JOB_CTX_CWD_MAX_LEN];
    uint64_t trace_id;
    m_timer_time_t submitted_at;
    m_timer_time_t started_at;
    m_timer_time_t completed_at;
    m_timer_deadline_t deadline;
    uint32_t priority_hint;
    job_ctx_user_attribute_t attributes[JOB_CTX_USER_ATTR_MAX];
    job_ctx_internal_flags_t internal;
    job_ctx_tls_t tls;
    portMUX_TYPE lock;
};

job_ctx_t *jctx_create(m_job_id_t job_id, m_job_id_t parent_job_id);
void jctx_acquire(job_ctx_t *ctx);
void jctx_release(job_ctx_t *ctx);
job_ctx_t *jctx_current(void);
m_job_id_t jctx_current_job_id(void);
void jctx_set_current(job_ctx_t *ctx);
job_ctx_field_policy_t jctx_field_policy(job_ctx_field_id_t field);
job_ctx_error_t jctx_get_field_kernel(job_ctx_t *ctx,
                                     job_ctx_field_id_t field,
                                     void *out_buf,
                                     size_t buf_size);
job_ctx_error_t jctx_set_field_kernel(job_ctx_t *ctx,
                                     job_ctx_field_id_t field,
                                     const void *value,
                                     size_t value_size);
job_ctx_error_t jctx_tls_set(job_ctx_t *ctx,
                             size_t slot,
                             void *value,
                             job_ctx_tls_destructor_t destructor);
void *jctx_tls_get(job_ctx_t *ctx, size_t slot);
void jctx_mark_cancelled(job_ctx_t *ctx);
bool jctx_is_cancelled(job_ctx_t *ctx);
void jctx_set_scheduler_state(job_ctx_t *ctx,
                              job_ctx_scheduler_state_t state);
void jctx_set_started(job_ctx_t *ctx, m_timer_time_t time);
void jctx_set_completed(job_ctx_t *ctx, m_timer_time_t time);

#ifdef __cplusplus
}
#endif

#endif /* MAGNOLIA_JOB_JCTX_H */
