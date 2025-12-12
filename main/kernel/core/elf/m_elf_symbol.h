/*
 * Kernel ELF symbol registry.
 */

#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define M_ELFSYM_EXPORT(_sym)     { #_sym, &_sym }
#define M_ELFSYM_END              { NULL,  NULL }

struct m_elfsym {
    const char  *name;
    const void  *sym;
};

uintptr_t m_elf_find_sym(const char *sym_name);
uintptr_t m_elf_register_symbol(const char *name, void *sym);

#ifdef __cplusplus
}
#endif

