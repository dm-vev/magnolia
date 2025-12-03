#ifndef MAGNOLIA_LIBEXEC_JOB_CTX_H
#define MAGNOLIA_LIBEXEC_JOB_CTX_H

#include "kernel/core/job/jctx_public.h"

#ifdef __cplusplus
extern "C" {
#endif

job_ctx_error_t job_field_get(m_job_id_t job,
                             job_ctx_field_id_t field,
                             void *out_buf,
                             size_t buf_size);

job_ctx_error_t job_field_set(m_job_id_t job,
                             job_ctx_field_id_t field,
                             const void *value,
                             size_t value_size);

#ifdef __cplusplus
}
#endif

#endif /* MAGNOLIA_LIBEXEC_JOB_CTX_H */
