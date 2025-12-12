/*
 * Kernel ELF platform hooks implementation.
 * Allocations are routed through Magnolia job allocator.
 */

#include <stdlib.h>

#include "kernel/core/job/jctx.h"
#include "kernel/core/memory/m_alloc.h"
#include "kernel/core/elf/m_elf_platform.h"
#include "kernel/core/elf/m_elf_loader.h"

void *m_elf_malloc(struct m_elf *elf, uint32_t n, bool exec)
{
    (void)exec;
    job_ctx_t *ctx = elf ? elf->ctx : jctx_current();
    return m_job_alloc(ctx, n);
}

void m_elf_free(struct m_elf *elf, void *ptr)
{
    job_ctx_t *ctx = elf ? elf->ctx : jctx_current();
    m_job_free(ctx, ptr);
}

#ifdef CONFIG_ELF_LOADER_CACHE_OFFSET
uintptr_t m_elf_remap_text(struct m_elf *elf, uintptr_t sym)
{
    /* No MMU remapping in baseline integration. */
    (void)elf;
    return sym;
}
#endif

#ifdef CONFIG_ELF_LOADER_LOAD_PSRAM
void m_elf_arch_flush(void)
{
    /* Baseline integration: no-op. */
}
#endif

