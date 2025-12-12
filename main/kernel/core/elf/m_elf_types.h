/*
 * Minimal ELF32 type definitions for Magnolia kernel ELF loader.
 * Derived from existing autonomous loader; no esp-elfloader runtime.
 */

#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include "sdkconfig.h"

#ifdef __cplusplus
extern "C" {
#endif

#define EI_NIDENT       16

/* Segment types */
#define PT_NULL         0
#define PT_LOAD         1
#define PT_DYNAMIC      2
#define PT_INTERP       3
#define PT_NOTE         4
#define PT_SHLIB        5
#define PT_PHDR         6
#define PT_TLS          7

/* Section types */
#define SHT_NULL        0
#define SHT_PROGBITS    1
#define SHT_SYMTAB      2
#define SHT_STRTAB      3
#define SHT_RELA        4
#define SHT_HASH        5
#define SHT_SYNAMIC     6
#define SHT_NOTE        7
#define SHT_NOBITS      8
#define SHT_REL         9

/* Section flags */
#define SHF_WRITE       1
#define SHF_ALLOC       2
#define SHF_EXECINSTR   4

/* Symbol types */
#define STT_NOTYPE      0
#define STT_OBJECT      1
#define STT_FUNC        2
#define STT_SECTION     3
#define STT_FILE        4
#define STT_COMMON      5
#define STT_TLS         6

/* Standard section names */
#define ELF_BSS         ".bss"
#define ELF_DATA        ".data"
#define ELF_RODATA      ".rodata"
#define ELF_TEXT        ".text"
#define ELF_DATA_REL_RO ".data.rel.ro"

#define ELF_SECS        5
#define ELF_SEC_TEXT    0
#define ELF_SEC_BSS     1
#define ELF_SEC_DATA    2
#define ELF_SEC_RODATA  3
#define ELF_SEC_DRLRO   4

#define ELF_ALIGN(_a, align_size) (((_a) + (align_size - 1)) & ~(align_size - 1))

#define ELF_R_SYM(i)            ((i) >> 8)
#define ELF_R_TYPE(i)           ((unsigned char)(i))
#define ELF_R_INFO(s, t)        (((s) << 8) + (unsigned char)(t))

typedef unsigned int    Elf32_Addr;
typedef unsigned int    Elf32_Off;
typedef unsigned int    Elf32_Word;
typedef unsigned short  Elf32_Half;
typedef int             Elf32_Sword;

typedef struct elf32_hdr {
    unsigned char   ident[EI_NIDENT];
    Elf32_Half      type;
    Elf32_Half      machine;
    Elf32_Word      version;
    Elf32_Addr      entry;
    Elf32_Off       phoff;
    Elf32_Off       shoff;
    Elf32_Word      flags;
    Elf32_Half      ehsize;
    Elf32_Half      phentsize;
    Elf32_Half      phnum;
    Elf32_Half      shentsize;
    Elf32_Half      shnum;
    Elf32_Half      shstrndx;
} elf32_hdr_t;

typedef struct elf32_phdr {
    Elf32_Word type;
    Elf32_Off  offset;
    Elf32_Addr vaddr;
    Elf32_Addr paddr;
    Elf32_Word filesz;
    Elf32_Word memsz;
    Elf32_Word flags;
    Elf32_Word align;
} elf32_phdr_t;

typedef struct elf32_shdr {
    Elf32_Word      name;
    Elf32_Word      type;
    Elf32_Word      flags;
    Elf32_Addr      addr;
    Elf32_Off       offset;
    Elf32_Word      size;
    Elf32_Word      link;
    Elf32_Word      info;
    Elf32_Word      addralign;
    Elf32_Word      entsize;
} elf32_shdr_t;

typedef struct elf32_sym {
    Elf32_Word      name;
    Elf32_Addr      value;
    Elf32_Word      size;
    unsigned char   info;
    unsigned char   other;
    Elf32_Half      shndx;
} elf32_sym_t;

typedef struct elf32_rela {
    Elf32_Addr      offset;
    Elf32_Word      info;
    Elf32_Sword     addend;
} elf32_rela_t;

typedef struct m_elf_sec {
    uintptr_t       v_addr;
    off_t           offset;
    uintptr_t       addr;
    size_t          size;
} m_elf_sec_t;

#ifdef __cplusplus
}
#endif

