#include "kernel/core/elf/m_elf_app_api.h"

#include "sdkconfig.h"

#include <errno.h>
#include <string.h>

#include "esp_heap_caps.h"

#if CONFIG_MAGNOLIA_ALLOC_ENABLED
#include "kernel/core/memory/m_alloc.h"
#endif

#if CONFIG_MAGNOLIA_JOB_ENABLED
#include "kernel/core/job/jctx.h"
#endif

int m_meminfo(magnolia_meminfo_t *info)
{
    if (info == NULL) {
        return -EINVAL;
    }

    uint32_t want = info->size;
    if (want == 0) {
        want = (uint32_t)sizeof(*info);
    }

    magnolia_meminfo_t out = {0};
    out.size = (uint32_t)sizeof(out);
    out.version = 1;

    out.heap_total_bytes = heap_caps_get_total_size(MALLOC_CAP_DEFAULT);
    out.heap_free_bytes = heap_caps_get_free_size(MALLOC_CAP_DEFAULT);
    out.heap_min_free_bytes = heap_caps_get_minimum_free_size(MALLOC_CAP_DEFAULT);
    out.heap_largest_free_block_bytes = heap_caps_get_largest_free_block(MALLOC_CAP_DEFAULT);

#if CONFIG_MAGNOLIA_ALLOC_ENABLED && CONFIG_MAGNOLIA_JOB_ENABLED
    job_ctx_t *ctx = jctx_current();
    if (ctx != NULL) {
        magnolia_alloc_job_stats_t stats;
        m_alloc_get_job_stats(ctx, &stats);
        out.job_used_bytes = stats.used_bytes;
        out.job_peak_bytes = stats.peak_bytes;
        out.job_capacity_bytes = stats.capacity_bytes;
        out.job_region_count = stats.region_count;
    }
#endif

    uint32_t copy = want;
    if (copy > (uint32_t)sizeof(out)) {
        copy = (uint32_t)sizeof(out);
    }
    memcpy(info, &out, copy);
    return 0;
}

