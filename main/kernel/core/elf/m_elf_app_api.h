#ifndef MAGNOLIA_ELF_APP_API_H
#define MAGNOLIA_ELF_APP_API_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Memory/heap statistics available to ELF applets.
 *
 * ABI notes:
 * - Caller sets `size` to sizeof(magnolia_meminfo_t) it expects.
 * - Kernel fills up to min(size, sizeof(magnolia_meminfo_t)).
 * - `version` is currently 1.
 */
typedef struct {
    uint32_t size;
    uint32_t version;

    size_t heap_total_bytes;
    size_t heap_free_bytes;
    size_t heap_min_free_bytes;
    size_t heap_largest_free_block_bytes;

    size_t job_capacity_bytes;
    size_t job_used_bytes;
    size_t job_peak_bytes;
    size_t job_region_count;
} magnolia_meminfo_t;

/**
 * @brief Populate memory statistics for the current system/job.
 *
 * Exported to ELF applets as `m_meminfo`.
 *
 * @return 0 on success, negative errno-style value on failure.
 */
int m_meminfo(magnolia_meminfo_t *info);

#ifdef __cplusplus
}
#endif

#endif /* MAGNOLIA_ELF_APP_API_H */

