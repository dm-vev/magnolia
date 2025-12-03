/*
 * Magnolia OS — Job Context Implementation
 */

#include "kernel/core/job/jctx.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#if (configNUM_THREAD_LOCAL_STORAGE_POINTERS == 0)
#error "Job context tracking requires thread-local storage pointers"
#endif

#define JCTX_TLS_TASK_INDEX 0

typedef struct {
    job_ctx_field_id_t id;
    job_ctx_field_type_t type;
    job_ctx_field_policy_t policy;
    size_t offset;
    size_t size;
} job_ctx_field_descriptor_t;

#define JCTX_ATTR_OFFSET(idx, member)                                   \
    (offsetof(job_ctx_t, attributes)                                   \
     + sizeof(job_ctx_user_attribute_t) * (idx)                        \
     + offsetof(job_ctx_user_attribute_t, member))

#define JCTX_TLS_SLOT_OFFSET(idx)                                        \
    (offsetof(job_ctx_t, tls.slots) + sizeof(void *) * (idx))

#define JCTX_TLS_DESTRUCTOR_OFFSET(idx)                                  \
    (offsetof(job_ctx_t, tls.destructors) + sizeof(job_ctx_tls_destructor_t) * (idx))

static const job_ctx_field_descriptor_t g_job_ctx_field_table[JOB_CTX_FIELD_COUNT] = {
    [JOB_CTX_FIELD_JOB_ID] = {
        .id = JOB_CTX_FIELD_JOB_ID,
        .type = JOB_CTX_FIELD_TYPE_RAW,
        .policy = JOB_CTX_FIELD_POLICY_PROTECTED,
        .offset = offsetof(job_ctx_t, job_id),
        .size = sizeof(((job_ctx_t *)0)->job_id),
    },
    [JOB_CTX_FIELD_PARENT_JOB_ID] = {
        .id = JOB_CTX_FIELD_PARENT_JOB_ID,
        .type = JOB_CTX_FIELD_TYPE_RAW,
        .policy = JOB_CTX_FIELD_POLICY_PROTECTED,
        .offset = offsetof(job_ctx_t, parent_job_id),
        .size = sizeof(((job_ctx_t *)0)->parent_job_id),
    },
    [JOB_CTX_FIELD_UID] = {
        .id = JOB_CTX_FIELD_UID,
        .type = JOB_CTX_FIELD_TYPE_RAW,
        .policy = JOB_CTX_FIELD_POLICY_PROTECTED,
        .offset = offsetof(job_ctx_t, uid),
        .size = sizeof(((job_ctx_t *)0)->uid),
    },
    [JOB_CTX_FIELD_GID] = {
        .id = JOB_CTX_FIELD_GID,
        .type = JOB_CTX_FIELD_TYPE_RAW,
        .policy = JOB_CTX_FIELD_POLICY_PROTECTED,
        .offset = offsetof(job_ctx_t, gid),
        .size = sizeof(((job_ctx_t *)0)->gid),
    },
    [JOB_CTX_FIELD_EUID] = {
        .id = JOB_CTX_FIELD_EUID,
        .type = JOB_CTX_FIELD_TYPE_RAW,
        .policy = JOB_CTX_FIELD_POLICY_PROTECTED,
        .offset = offsetof(job_ctx_t, euid),
        .size = sizeof(((job_ctx_t *)0)->euid),
    },
    [JOB_CTX_FIELD_EGID] = {
        .id = JOB_CTX_FIELD_EGID,
        .type = JOB_CTX_FIELD_TYPE_RAW,
        .policy = JOB_CTX_FIELD_POLICY_PROTECTED,
        .offset = offsetof(job_ctx_t, egid),
        .size = sizeof(((job_ctx_t *)0)->egid),
    },
    [JOB_CTX_FIELD_CWD] = {
        .id = JOB_CTX_FIELD_CWD,
        .type = JOB_CTX_FIELD_TYPE_STRING,
        .policy = JOB_CTX_FIELD_POLICY_PUBLIC,
        .offset = offsetof(job_ctx_t, cwd),
        .size = sizeof(((job_ctx_t *)0)->cwd),
    },
    [JOB_CTX_FIELD_TRACE_ID] = {
        .id = JOB_CTX_FIELD_TRACE_ID,
        .type = JOB_CTX_FIELD_TYPE_RAW,
        .policy = JOB_CTX_FIELD_POLICY_PROTECTED,
        .offset = offsetof(job_ctx_t, trace_id),
        .size = sizeof(((job_ctx_t *)0)->trace_id),
    },
    [JOB_CTX_FIELD_SUBMITTED_AT] = {
        .id = JOB_CTX_FIELD_SUBMITTED_AT,
        .type = JOB_CTX_FIELD_TYPE_RAW,
        .policy = JOB_CTX_FIELD_POLICY_PROTECTED,
        .offset = offsetof(job_ctx_t, submitted_at),
        .size = sizeof(((job_ctx_t *)0)->submitted_at),
    },
    [JOB_CTX_FIELD_STARTED_AT] = {
        .id = JOB_CTX_FIELD_STARTED_AT,
        .type = JOB_CTX_FIELD_TYPE_RAW,
        .policy = JOB_CTX_FIELD_POLICY_PROTECTED,
        .offset = offsetof(job_ctx_t, started_at),
        .size = sizeof(((job_ctx_t *)0)->started_at),
    },
    [JOB_CTX_FIELD_COMPLETED_AT] = {
        .id = JOB_CTX_FIELD_COMPLETED_AT,
        .type = JOB_CTX_FIELD_TYPE_RAW,
        .policy = JOB_CTX_FIELD_POLICY_PROTECTED,
        .offset = offsetof(job_ctx_t, completed_at),
        .size = sizeof(((job_ctx_t *)0)->completed_at),
    },
    [JOB_CTX_FIELD_DEADLINE] = {
        .id = JOB_CTX_FIELD_DEADLINE,
        .type = JOB_CTX_FIELD_TYPE_RAW,
        .policy = JOB_CTX_FIELD_POLICY_PROTECTED,
        .offset = offsetof(job_ctx_t, deadline),
        .size = sizeof(((job_ctx_t *)0)->deadline),
    },
    [JOB_CTX_FIELD_PRIORITY_HINT] = {
        .id = JOB_CTX_FIELD_PRIORITY_HINT,
        .type = JOB_CTX_FIELD_TYPE_RAW,
        .policy = JOB_CTX_FIELD_POLICY_PUBLIC,
        .offset = offsetof(job_ctx_t, priority_hint),
        .size = sizeof(((job_ctx_t *)0)->priority_hint),
    },
    [JOB_CTX_FIELD_USER_ATTR_KEY_0] = {
        .id = JOB_CTX_FIELD_USER_ATTR_KEY_0,
        .type = JOB_CTX_FIELD_TYPE_STRING,
        .policy = JOB_CTX_FIELD_POLICY_PUBLIC,
        .offset = JCTX_ATTR_OFFSET(0, key),
        .size = sizeof(((job_ctx_user_attribute_t *)0)->key),
    },
    [JOB_CTX_FIELD_USER_ATTR_VALUE_0] = {
        .id = JOB_CTX_FIELD_USER_ATTR_VALUE_0,
        .type = JOB_CTX_FIELD_TYPE_STRING,
        .policy = JOB_CTX_FIELD_POLICY_PUBLIC,
        .offset = JCTX_ATTR_OFFSET(0, value),
        .size = sizeof(((job_ctx_user_attribute_t *)0)->value),
    },
    [JOB_CTX_FIELD_USER_ATTR_KEY_1] = {
        .id = JOB_CTX_FIELD_USER_ATTR_KEY_1,
        .type = JOB_CTX_FIELD_TYPE_STRING,
        .policy = JOB_CTX_FIELD_POLICY_PUBLIC,
        .offset = JCTX_ATTR_OFFSET(1, key),
        .size = sizeof(((job_ctx_user_attribute_t *)0)->key),
    },
    [JOB_CTX_FIELD_USER_ATTR_VALUE_1] = {
        .id = JOB_CTX_FIELD_USER_ATTR_VALUE_1,
        .type = JOB_CTX_FIELD_TYPE_STRING,
        .policy = JOB_CTX_FIELD_POLICY_PUBLIC,
        .offset = JCTX_ATTR_OFFSET(1, value),
        .size = sizeof(((job_ctx_user_attribute_t *)0)->value),
    },
    [JOB_CTX_FIELD_USER_ATTR_KEY_2] = {
        .id = JOB_CTX_FIELD_USER_ATTR_KEY_2,
        .type = JOB_CTX_FIELD_TYPE_STRING,
        .policy = JOB_CTX_FIELD_POLICY_PUBLIC,
        .offset = JCTX_ATTR_OFFSET(2, key),
        .size = sizeof(((job_ctx_user_attribute_t *)0)->key),
    },
    [JOB_CTX_FIELD_USER_ATTR_VALUE_2] = {
        .id = JOB_CTX_FIELD_USER_ATTR_VALUE_2,
        .type = JOB_CTX_FIELD_TYPE_STRING,
        .policy = JOB_CTX_FIELD_POLICY_PUBLIC,
        .offset = JCTX_ATTR_OFFSET(2, value),
        .size = sizeof(((job_ctx_user_attribute_t *)0)->value),
    },
    [JOB_CTX_FIELD_USER_ATTR_KEY_3] = {
        .id = JOB_CTX_FIELD_USER_ATTR_KEY_3,
        .type = JOB_CTX_FIELD_TYPE_STRING,
        .policy = JOB_CTX_FIELD_POLICY_PUBLIC,
        .offset = JCTX_ATTR_OFFSET(3, key),
        .size = sizeof(((job_ctx_user_attribute_t *)0)->key),
    },
    [JOB_CTX_FIELD_USER_ATTR_VALUE_3] = {
        .id = JOB_CTX_FIELD_USER_ATTR_VALUE_3,
        .type = JOB_CTX_FIELD_TYPE_STRING,
        .policy = JOB_CTX_FIELD_POLICY_PUBLIC,
        .offset = JCTX_ATTR_OFFSET(3, value),
        .size = sizeof(((job_ctx_user_attribute_t *)0)->value),
    },
    [JOB_CTX_FIELD_INTERNAL_CANCELLED] = {
        .id = JOB_CTX_FIELD_INTERNAL_CANCELLED,
        .type = JOB_CTX_FIELD_TYPE_RAW,
        .policy = JOB_CTX_FIELD_POLICY_PRIVATE,
        .offset = offsetof(job_ctx_t, internal.cancelled),
        .size = sizeof(((job_ctx_internal_flags_t *)0)->cancelled),
    },
    [JOB_CTX_FIELD_INTERNAL_SCHED_STATE] = {
        .id = JOB_CTX_FIELD_INTERNAL_SCHED_STATE,
        .type = JOB_CTX_FIELD_TYPE_RAW,
        .policy = JOB_CTX_FIELD_POLICY_PRIVATE,
        .offset = offsetof(job_ctx_t, internal.scheduler_state),
        .size = sizeof(((job_ctx_internal_flags_t *)0)->scheduler_state),
    },
    [JOB_CTX_FIELD_INTERNAL_REFCOUNT] = {
        .id = JOB_CTX_FIELD_INTERNAL_REFCOUNT,
        .type = JOB_CTX_FIELD_TYPE_RAW,
        .policy = JOB_CTX_FIELD_POLICY_PRIVATE,
        .offset = offsetof(job_ctx_t, internal.refcount),
        .size = sizeof(((job_ctx_internal_flags_t *)0)->refcount),
    },
    [JOB_CTX_FIELD_TLS_SLOT_VALUE_0] = {
        .id = JOB_CTX_FIELD_TLS_SLOT_VALUE_0,
        .type = JOB_CTX_FIELD_TYPE_RAW,
        .policy = JOB_CTX_FIELD_POLICY_PRIVATE,
        .offset = JCTX_TLS_SLOT_OFFSET(0),
        .size = sizeof(void *),
    },
    [JOB_CTX_FIELD_TLS_SLOT_VALUE_1] = {
        .id = JOB_CTX_FIELD_TLS_SLOT_VALUE_1,
        .type = JOB_CTX_FIELD_TYPE_RAW,
        .policy = JOB_CTX_FIELD_POLICY_PRIVATE,
        .offset = JCTX_TLS_SLOT_OFFSET(1),
        .size = sizeof(void *),
    },
    [JOB_CTX_FIELD_TLS_SLOT_VALUE_2] = {
        .id = JOB_CTX_FIELD_TLS_SLOT_VALUE_2,
        .type = JOB_CTX_FIELD_TYPE_RAW,
        .policy = JOB_CTX_FIELD_POLICY_PRIVATE,
        .offset = JCTX_TLS_SLOT_OFFSET(2),
        .size = sizeof(void *),
    },
    [JOB_CTX_FIELD_TLS_SLOT_VALUE_3] = {
        .id = JOB_CTX_FIELD_TLS_SLOT_VALUE_3,
        .type = JOB_CTX_FIELD_TYPE_RAW,
        .policy = JOB_CTX_FIELD_POLICY_PRIVATE,
        .offset = JCTX_TLS_SLOT_OFFSET(3),
        .size = sizeof(void *),
    },
    [JOB_CTX_FIELD_TLS_SLOT_DESTRUCTOR_0] = {
        .id = JOB_CTX_FIELD_TLS_SLOT_DESTRUCTOR_0,
        .type = JOB_CTX_FIELD_TYPE_RAW,
        .policy = JOB_CTX_FIELD_POLICY_PRIVATE,
        .offset = JCTX_TLS_DESTRUCTOR_OFFSET(0),
        .size = sizeof(job_ctx_tls_destructor_t),
    },
    [JOB_CTX_FIELD_TLS_SLOT_DESTRUCTOR_1] = {
        .id = JOB_CTX_FIELD_TLS_SLOT_DESTRUCTOR_1,
        .type = JOB_CTX_FIELD_TYPE_RAW,
        .policy = JOB_CTX_FIELD_POLICY_PRIVATE,
        .offset = JCTX_TLS_DESTRUCTOR_OFFSET(1),
        .size = sizeof(job_ctx_tls_destructor_t),
    },
    [JOB_CTX_FIELD_TLS_SLOT_DESTRUCTOR_2] = {
        .id = JOB_CTX_FIELD_TLS_SLOT_DESTRUCTOR_2,
        .type = JOB_CTX_FIELD_TYPE_RAW,
        .policy = JOB_CTX_FIELD_POLICY_PRIVATE,
        .offset = JCTX_TLS_DESTRUCTOR_OFFSET(2),
        .size = sizeof(job_ctx_tls_destructor_t),
    },
    [JOB_CTX_FIELD_TLS_SLOT_DESTRUCTOR_3] = {
        .id = JOB_CTX_FIELD_TLS_SLOT_DESTRUCTOR_3,
        .type = JOB_CTX_FIELD_TYPE_RAW,
        .policy = JOB_CTX_FIELD_POLICY_PRIVATE,
        .offset = JCTX_TLS_DESTRUCTOR_OFFSET(3),
        .size = sizeof(job_ctx_tls_destructor_t),
    },
};

static const job_ctx_field_descriptor_t *jctx_descriptor(job_ctx_field_id_t field)
{
    if ((size_t)field >= JOB_CTX_FIELD_COUNT) {
        return NULL;
    }
    return &g_job_ctx_field_table[field];
}

static inline void jctx_lock_ctx(job_ctx_t *ctx)
{
    if (ctx == NULL) {
        return;
    }
    portENTER_CRITICAL(&ctx->lock);
}

static inline void jctx_unlock_ctx(job_ctx_t *ctx)
{
    if (ctx == NULL) {
        return;
    }
    portEXIT_CRITICAL(&ctx->lock);
}

static job_ctx_error_t jctx_copy_to_buffer(const job_ctx_field_descriptor_t *desc,
                                            const void *source,
                                            void *dest,
                                            size_t buf_size)
{
    if (buf_size < desc->size) {
        return JOB_CTX_ERR_BUFFER_TOO_SMALL;
    }
    memcpy(dest, source, desc->size);
    return JOB_CTX_OK;
}

static job_ctx_error_t jctx_copy_from_buffer(const job_ctx_field_descriptor_t *desc,
                                              job_ctx_t *ctx,
                                              const void *value,
                                              size_t value_size)
{
    uint8_t *dest = (uint8_t *)ctx + desc->offset;
    if (desc->type == JOB_CTX_FIELD_TYPE_STRING) {
        size_t allowed = desc->size;
        if (allowed == 0) {
            return JOB_CTX_ERR_INVALID_FIELD;
        }
        size_t copy_len = (value == NULL) ? 0 : value_size;
        if (copy_len >= allowed) {
            copy_len = allowed - 1;
        }
        memset(dest, 0, allowed);
        if (copy_len > 0 && value != NULL) {
            memcpy(dest, value, copy_len);
        }
        dest[copy_len] = '\0';
        return JOB_CTX_OK;
    }

    if (value == NULL || value_size != desc->size) {
        return JOB_CTX_ERR_BUFFER_TOO_SMALL;
    }
    memcpy(dest, value, desc->size);
    return JOB_CTX_OK;
}

job_ctx_t *jctx_create(m_job_id_t job_id, m_job_id_t parent_job_id)
{
    job_ctx_t *ctx = pvPortMalloc(sizeof(*ctx));
    if (ctx == NULL) {
        return NULL;
    }
    memset(ctx, 0, sizeof(*ctx));
    ctx->job_id = job_id;
    ctx->parent_job_id = parent_job_id;
    ctx->uid = 0;
    ctx->gid = 0;
    ctx->euid = 0;
    ctx->egid = 0;
    ctx->cwd[0] = '/';
    ctx->cwd[1] = '\0';
    ctx->trace_id = ((uint64_t)(uintptr_t)job_id << 32) ^ m_timer_get_monotonic();
    ctx->submitted_at = m_timer_get_monotonic();
    ctx->deadline.infinite = true;
    ctx->internal.scheduler_state = JOB_CTX_SCHED_STATE_PENDING;
    ctx->internal.refcount = 1;
    ctx->internal.cancelled = false;
    ctx->lock = (portMUX_TYPE)portMUX_INITIALIZER_UNLOCKED;
    return ctx;
}

void jctx_acquire(job_ctx_t *ctx)
{
    if (ctx == NULL) {
        return;
    }
    jctx_lock_ctx(ctx);
    ++ctx->internal.refcount;
    jctx_unlock_ctx(ctx);
}

void jctx_release(job_ctx_t *ctx)
{
    if (ctx == NULL) {
        return;
    }

    job_ctx_tls_destructor_t dtors[JOB_CTX_TLS_SLOT_COUNT];
    void *slots[JOB_CTX_TLS_SLOT_COUNT];
    bool should_free = false;

    jctx_lock_ctx(ctx);
    if (ctx->internal.refcount == 0) {
        jctx_unlock_ctx(ctx);
        return;
    }
    --ctx->internal.refcount;
    if (ctx->internal.refcount == 0) {
        should_free = true;
        for (size_t i = 0; i < JOB_CTX_TLS_SLOT_COUNT; ++i) {
            slots[i] = ctx->tls.slots[i];
            dtors[i] = ctx->tls.destructors[i];
            ctx->tls.slots[i] = NULL;
            ctx->tls.destructors[i] = NULL;
        }
    }
    jctx_unlock_ctx(ctx);

    if (should_free) {
        for (size_t i = 0; i < JOB_CTX_TLS_SLOT_COUNT; ++i) {
            if (slots[i] != NULL && dtors[i] != NULL) {
                dtors[i](slots[i]);
            }
        }
        vPortFree(ctx);
    }
}

job_ctx_error_t jctx_tls_set(job_ctx_t *ctx,
                             size_t slot,
                             void *value,
                             job_ctx_tls_destructor_t destructor)
{
    if (ctx == NULL || slot >= JOB_CTX_TLS_SLOT_COUNT) {
        return JOB_CTX_ERR_INVALID_PARAM;
    }

    void *old = NULL;
    job_ctx_tls_destructor_t old_dtor = NULL;

    jctx_lock_ctx(ctx);
    old = ctx->tls.slots[slot];
    old_dtor = ctx->tls.destructors[slot];
    ctx->tls.slots[slot] = value;
    ctx->tls.destructors[slot] = destructor;
    jctx_unlock_ctx(ctx);

    if (old != NULL && old_dtor != NULL) {
        old_dtor(old);
    }
    return JOB_CTX_OK;
}

void *jctx_tls_get(job_ctx_t *ctx, size_t slot)
{
    if (ctx == NULL || slot >= JOB_CTX_TLS_SLOT_COUNT) {
        return NULL;
    }

    jctx_lock_ctx(ctx);
    void *value = ctx->tls.slots[slot];
    jctx_unlock_ctx(ctx);
    return value;
}

static inline void jctx_store_current(job_ctx_t *ctx)
{
    vTaskSetThreadLocalStoragePointer(NULL, JCTX_TLS_TASK_INDEX, ctx);
}

static inline job_ctx_t *jctx_load_current(void)
{
    return (job_ctx_t *)pvTaskGetThreadLocalStoragePointer(NULL, JCTX_TLS_TASK_INDEX);
}

job_ctx_t *jctx_current(void)
{
    return jctx_load_current();
}

m_job_id_t jctx_current_job_id(void)
{
    job_ctx_t *ctx = jctx_current();
    return ctx ? ctx->job_id : NULL;
}

void jctx_set_current(job_ctx_t *ctx)
{
    jctx_store_current(ctx);
}

job_ctx_error_t jctx_get_field_kernel(job_ctx_t *ctx,
                                     job_ctx_field_id_t field,
                                     void *out_buf,
                                     size_t buf_size)
{
    if (ctx == NULL || out_buf == NULL) {
        return JOB_CTX_ERR_INVALID_PARAM;
    }
    const job_ctx_field_descriptor_t *desc = jctx_descriptor(field);
    if (desc == NULL) {
        return JOB_CTX_ERR_INVALID_FIELD;
    }

    jctx_lock_ctx(ctx);
    job_ctx_error_t err = jctx_copy_to_buffer(desc,
                                             (const uint8_t *)ctx + desc->offset,
                                             out_buf,
                                             buf_size);
    jctx_unlock_ctx(ctx);
    return err;
}

job_ctx_error_t jctx_set_field_kernel(job_ctx_t *ctx,
                                     job_ctx_field_id_t field,
                                     const void *value,
                                     size_t value_size)
{
    if (ctx == NULL) {
        return JOB_CTX_ERR_INVALID_PARAM;
    }
    const job_ctx_field_descriptor_t *desc = jctx_descriptor(field);
    if (desc == NULL) {
        return JOB_CTX_ERR_INVALID_FIELD;
    }
    if (value == NULL && desc->type != JOB_CTX_FIELD_TYPE_STRING) {
        return JOB_CTX_ERR_INVALID_PARAM;
    }

    if (desc->type != JOB_CTX_FIELD_TYPE_STRING && value_size != desc->size) {
        return JOB_CTX_ERR_BUFFER_TOO_SMALL;
    }

    jctx_lock_ctx(ctx);
    job_ctx_error_t err = jctx_copy_from_buffer(desc, ctx, value, value_size);
    jctx_unlock_ctx(ctx);
    return err;
}

void jctx_mark_cancelled(job_ctx_t *ctx)
{
    if (ctx == NULL) {
        return;
    }
    jctx_lock_ctx(ctx);
    ctx->internal.cancelled = true;
    ctx->internal.scheduler_state = JOB_CTX_SCHED_STATE_CANCELED;
    jctx_unlock_ctx(ctx);
}

bool jctx_is_cancelled(job_ctx_t *ctx)
{
    if (ctx == NULL) {
        return false;
    }
    jctx_lock_ctx(ctx);
    bool value = ctx->internal.cancelled;
    jctx_unlock_ctx(ctx);
    return value;
}

void jctx_set_scheduler_state(job_ctx_t *ctx,
                              job_ctx_scheduler_state_t state)
{
    if (ctx == NULL) {
        return;
    }
    jctx_lock_ctx(ctx);
    ctx->internal.scheduler_state = state;
    jctx_unlock_ctx(ctx);
}

void jctx_set_started(job_ctx_t *ctx, m_timer_time_t time)
{
    if (ctx == NULL) {
        return;
    }
    jctx_lock_ctx(ctx);
    ctx->started_at = time;
    jctx_unlock_ctx(ctx);
}

void jctx_set_completed(job_ctx_t *ctx, m_timer_time_t time)
{
    if (ctx == NULL) {
        return;
    }
    jctx_lock_ctx(ctx);
    ctx->completed_at = time;
    jctx_unlock_ctx(ctx);
}

job_ctx_field_policy_t jctx_field_policy(job_ctx_field_id_t field)
{
    const job_ctx_field_descriptor_t *desc = jctx_descriptor(field);
    if (desc == NULL) {
        return JOB_CTX_FIELD_POLICY_PRIVATE;
    }
    return desc->policy;
}
