/*
 * Kernel ELF platform hooks.
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>

#include "kernel/core/elf/m_elf_types.h"

#ifdef __cplusplus
extern "C" {
#endif

struct m_elf;

void *m_elf_malloc(struct m_elf *elf, uint32_t n, bool exec);
void m_elf_free(struct m_elf *elf, void *ptr);

int m_elf_arch_relocate(struct m_elf *elf, const elf32_rela_t *rela,
                        const elf32_sym_t *sym, uint32_t addr);

#ifdef CONFIG_ELF_LOADER_CACHE_OFFSET
uintptr_t m_elf_remap_text(struct m_elf *elf, uintptr_t sym);
#endif

#ifdef CONFIG_ELF_LOADER_LOAD_PSRAM
void m_elf_arch_flush(void);
#endif

#ifdef __cplusplus
}
#endif

