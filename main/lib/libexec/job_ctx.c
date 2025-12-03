#include "libexec/job_ctx.h"

job_ctx_error_t job_field_get(m_job_id_t job,
                             job_ctx_field_id_t field,
                             void *out_buf,
                             size_t buf_size)
{
    return m_job_field_get(job, field, out_buf, buf_size);
}

job_ctx_error_t job_field_set(m_job_id_t job,
                             job_ctx_field_id_t field,
                             const void *value,
                             size_t value_size)
{
    return m_job_field_set(job, field, value, value_size);
}
