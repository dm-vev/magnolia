#ifndef MAGNOLIA_MEMORY_M_ALLOC_H
#define MAGNOLIA_MEMORY_M_ALLOC_H

#include "sdkconfig.h"

#include <stddef.h>
#include <stdbool.h>
#include <stdlib.h>

#include "kernel/core/job/jctx.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Job-local allocator statistics exposed to diagnostics.
 */
typedef struct {
    size_t used_bytes;
    size_t peak_bytes;
    size_t capacity_bytes;
    size_t region_count;
} magnolia_alloc_job_stats_t;

/**
 * @brief Global allocator statistics for diagnostics.
 */
typedef struct {
    size_t total_regions;
    size_t total_psram_bytes;
    size_t total_allocations;
    size_t total_frees;
} magnolia_alloc_global_stats_t;

/**
 * @brief Initialize Magnolia allocator subsystems (system job context, stats, etc.).
 */
void m_alloc_init(void);

void *m_job_alloc(job_ctx_t *ctx, size_t size);
void *m_job_calloc(job_ctx_t *ctx, size_t nmemb, size_t size);
void *m_job_realloc(job_ctx_t *ctx, void *ptr, size_t new_size);
void m_job_free(job_ctx_t *ctx, void *ptr);

/**
 * @brief Teardown heap structures attached to a job context.
 */
void m_alloc_teardown_job_ctx(job_ctx_t *ctx);

void m_alloc_get_job_stats(job_ctx_t *ctx, magnolia_alloc_job_stats_t *out);
void m_alloc_get_global_stats(magnolia_alloc_global_stats_t *out);

#ifdef __cplusplus
}
#endif

#endif /* MAGNOLIA_MEMORY_M_ALLOC_H */
