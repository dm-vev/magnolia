/*
 * Magnolia kernel ELF loader (baseline).
 * Loads ELF32 from memory or file, maps via Magnolia allocator, runs in job context.
 */

#pragma once

#include <stdint.h>

#include "kernel/core/job/jctx.h"
#include "kernel/core/elf/m_elf_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct m_elf {
    unsigned char   *psegment;
    uint32_t         svaddr;
    unsigned char   *ptext;
    unsigned char   *pdata;
    m_elf_sec_t      sec[ELF_SECS];
    uintptr_t        load_bias;
    struct {
        uintptr_t vaddr;
        uintptr_t addr;
        uint32_t size;
    } maps[8];
    uint32_t map_count;
    struct {
        void *ptr;
    } allocs[8];
    uint32_t alloc_count;
    void          (**preinit_array)(void);
    uint32_t        preinit_count;
    void          (**init_array)(void);
    uint32_t        init_count;
    void          (**fini_array)(void);
    uint32_t        fini_count;
    int (*entry)(int argc, char *argv[]);
    job_ctx_t       *ctx;
} m_elf_t;

int m_elf_init(m_elf_t *elf, job_ctx_t *ctx);
int m_elf_relocate(m_elf_t *elf, const uint8_t *pbuf, size_t len);
int m_elf_request(m_elf_t *elf, int opt, int argc, char *argv[]);
void m_elf_deinit(m_elf_t *elf);

/* Map an ELF virtual address to a loaded host address (0 if unmapped). */
uintptr_t m_elf_map_vaddr(m_elf_t *elf, uintptr_t vaddr);

/* Convenience: load+run ELF buffer in current job context. */
int m_elf_run_buffer(const uint8_t *pbuf, size_t len, int argc, char *argv[], int *out_rc);

/* Convenience: read ELF from VFS path and run in current job context. */
int m_elf_run_file(const char *path, int argc, char *argv[], int *out_rc);

#ifdef __cplusplus
}
#endif
