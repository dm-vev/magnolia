/*
 * Kernel ELF platform hooks implementation.
 * Allocations use heap capabilities to ensure executable sections land in
 * instruction-accessible memory (required for Xtensa/RISC-V instruction fetch).
 */

#include <stdlib.h>
#include <stdint.h>

#include "esp_heap_caps.h"
#include "esp_log.h"

#include "kernel/core/job/jctx.h"
#include "kernel/core/memory/m_alloc.h"
#include "kernel/core/elf/m_elf_platform.h"
#include "kernel/core/elf/m_elf_loader.h"

typedef struct {
    uint32_t magic;
    uint32_t flags;
    job_ctx_t *ctx;
    uint32_t reserved;
} m_elf_alloc_hdr_t;

static const char *TAG = "m_elf_platform";

#define M_ELF_ALLOC_MAGIC 0x454C4641u /* 'ELFA' */
#define M_ELF_ALLOC_FLAG_EXEC 0x1u
#define M_ELF_ALLOC_FLAG_HEAPCAPS 0x2u

static void m_elf_log_exec_caps(uint32_t n)
{
    size_t free_exec = heap_caps_get_free_size(MALLOC_CAP_EXEC);
    size_t largest_exec = heap_caps_get_largest_free_block(MALLOC_CAP_EXEC);
    size_t free_spiram_exec = heap_caps_get_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_EXEC);
    size_t largest_spiram_exec = heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM | MALLOC_CAP_EXEC);
    ESP_LOGE(TAG,
             "ELF exec alloc failed size=0x%x free_exec=%u largest_exec=%u free_spiram_exec=%u largest_spiram_exec=%u",
             (unsigned)n,
             (unsigned)free_exec,
             (unsigned)largest_exec,
             (unsigned)free_spiram_exec,
             (unsigned)largest_spiram_exec);
}

void *m_elf_malloc(struct m_elf *elf, uint32_t n, bool exec)
{
    job_ctx_t *ctx = elf ? elf->ctx : jctx_current();
    size_t total = sizeof(m_elf_alloc_hdr_t) + (size_t)n;

    void *raw = NULL;
    uint32_t flags = 0;

#if CONFIG_ELF_LOADER_BUS_ADDRESS_MIRROR
#ifdef CONFIG_ELF_LOADER_LOAD_PSRAM
    if (exec) {
        raw = heap_caps_malloc(total, MALLOC_CAP_EXEC);
        flags |= M_ELF_ALLOC_FLAG_EXEC;
#if CONFIG_SPIRAM_FETCH_INSTRUCTIONS
        if (raw == NULL) {
            raw = heap_caps_malloc(total, MALLOC_CAP_SPIRAM | MALLOC_CAP_EXEC);
            if (raw != NULL) {
                flags |= M_ELF_ALLOC_FLAG_EXEC | M_ELF_ALLOC_FLAG_HEAPCAPS;
            }
        }
#endif
    } else {
        raw = heap_caps_malloc(total, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (raw != NULL) {
            flags |= M_ELF_ALLOC_FLAG_HEAPCAPS;
        }
        if (raw == NULL) {
            raw = m_job_alloc(ctx, total);
            if (raw == NULL) {
                raw = heap_caps_malloc(total, MALLOC_CAP_8BIT);
                flags |= M_ELF_ALLOC_FLAG_HEAPCAPS;
            }
        }
    }
#else
    if (exec) {
        raw = heap_caps_malloc(total, MALLOC_CAP_EXEC);
        flags |= M_ELF_ALLOC_FLAG_EXEC;
#if CONFIG_SPIRAM_FETCH_INSTRUCTIONS
        if (raw == NULL) {
            raw = heap_caps_malloc(total, MALLOC_CAP_SPIRAM | MALLOC_CAP_EXEC);
            if (raw != NULL) {
                flags |= M_ELF_ALLOC_FLAG_EXEC | M_ELF_ALLOC_FLAG_HEAPCAPS;
            }
        }
#endif
    } else {
        raw = m_job_alloc(ctx, total);
        if (raw == NULL) {
            raw = heap_caps_malloc(total, MALLOC_CAP_8BIT);
            flags |= M_ELF_ALLOC_FLAG_HEAPCAPS;
        }
    }
#endif
#else
#ifdef CONFIG_ELF_LOADER_LOAD_PSRAM
    if (exec) {
        raw = heap_caps_malloc(total, MALLOC_CAP_EXEC);
        flags |= M_ELF_ALLOC_FLAG_EXEC;
    } else {
        raw = heap_caps_malloc(total, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (raw != NULL) {
            flags |= M_ELF_ALLOC_FLAG_HEAPCAPS;
        }
        if (raw == NULL) {
            raw = m_job_alloc(ctx, total);
            if (raw == NULL) {
                raw = heap_caps_malloc(total, MALLOC_CAP_8BIT);
                flags |= M_ELF_ALLOC_FLAG_HEAPCAPS;
            }
        }
    }
#else
    if (exec) {
        raw = heap_caps_malloc(total, MALLOC_CAP_EXEC);
        flags |= M_ELF_ALLOC_FLAG_EXEC;
    } else {
        raw = m_job_alloc(ctx, total);
        if (raw == NULL) {
            raw = heap_caps_malloc(total, MALLOC_CAP_8BIT);
            flags |= M_ELF_ALLOC_FLAG_HEAPCAPS;
        }
    }
#endif
#endif

    if (raw == NULL) {
        if (exec) {
            m_elf_log_exec_caps(n);
        }
        return NULL;
    }

    m_elf_alloc_hdr_t *hdr = (m_elf_alloc_hdr_t *)raw;
    hdr->magic = M_ELF_ALLOC_MAGIC;
    hdr->flags = flags;
    hdr->ctx = ctx;
    hdr->reserved = 0;

    return (void *)(hdr + 1);
}

void m_elf_free(struct m_elf *elf, void *ptr)
{
    (void)elf;
    if (ptr == NULL) {
        return;
    }

    m_elf_alloc_hdr_t *hdr = ((m_elf_alloc_hdr_t *)ptr) - 1;
    if (hdr->magic != M_ELF_ALLOC_MAGIC) {
        /* Defensive: only free pointers allocated via m_elf_malloc. */
        return;
    }

    hdr->magic = 0;
    if (hdr->flags & (M_ELF_ALLOC_FLAG_EXEC | M_ELF_ALLOC_FLAG_HEAPCAPS)) {
        heap_caps_free(hdr);
    } else {
        m_job_free(hdr->ctx, hdr);
    }
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
