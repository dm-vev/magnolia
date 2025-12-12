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
    int (*entry)(int argc, char *argv[]);
    job_ctx_t       *ctx;
} m_elf_t;

int m_elf_init(m_elf_t *elf, job_ctx_t *ctx);
int m_elf_relocate(m_elf_t *elf, const uint8_t *pbuf, size_t len);
int m_elf_request(m_elf_t *elf, int opt, int argc, char *argv[]);
void m_elf_deinit(m_elf_t *elf);

/* Convenience: load+run ELF buffer in current job context. */
int m_elf_run_buffer(const uint8_t *pbuf, size_t len, int argc, char *argv[], int *out_rc);

/* Convenience: read ELF from VFS path and run in current job context. */
int m_elf_run_file(const char *path, int argc, char *argv[], int *out_rc);

#ifdef __cplusplus
}
#endif
