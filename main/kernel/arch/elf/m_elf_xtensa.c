/*
 * Xtensa relocation support for Magnolia kernel ELF loader.
 */

#include <assert.h>
#include <stdint.h>
#include <sys/errno.h>

#include "esp_log.h"

#include "kernel/core/elf/m_elf_loader.h"
#include "kernel/core/elf/m_elf_platform.h"

#define R_XTENSA_RELATIVE       5
#define R_XTENSA_GLOB_DAT       3
#define R_XTENSA_JMP_SLOT       4
#define R_XTENSA_RTLD           2
#define STT_FUNC                2

static const char *TAG = "m_elf_arch";

int m_elf_arch_relocate(struct m_elf *elf, const elf32_rela_t *rela,
                        const elf32_sym_t *sym, uint32_t addr)
{
    uint32_t val;
    uint32_t *where;

    assert(elf && rela);

    where = (uint32_t *)m_elf_map_vaddr((m_elf_t *)elf, rela->offset);
    if (!where) {
        return -EINVAL;
    }

    ESP_LOGD(TAG, "type=%d where=%p addr=0x%x off=0x%x",
             ELF_R_TYPE(rela->info), where, (int)addr, (int)rela->offset);

    switch (ELF_R_TYPE(rela->info)) {
    case R_XTENSA_RELATIVE:
        /*
         * Xtensa toolchains often place the relative addend in the relocation
         * target word (literal pool) rather than in rela->addend (which is
         * frequently 0). Treat the current word value as the addend when
         * explicit addends are absent.
         */
        val = (uint32_t)m_elf_map_vaddr((m_elf_t *)elf,
                                        (rela->addend != 0) ? (uintptr_t)rela->addend : (uintptr_t)(*where));
        if (val == 0) {
            return -EINVAL;
        }
#ifdef CONFIG_ELF_LOADER_CACHE_OFFSET
        *where = m_elf_remap_text((m_elf_t *)elf, val);
#else
        *where = val;
#endif
        break;
    case R_XTENSA_RTLD:
        break;
    case R_XTENSA_GLOB_DAT:
    case R_XTENSA_JMP_SLOT:
#ifdef CONFIG_ELF_LOADER_CACHE_OFFSET
        if (sym != NULL && ELF32_ST_TYPE(sym->info) == STT_FUNC) {
            *where = m_elf_remap_text((m_elf_t *)elf, addr);
        } else {
            *where = addr;
        }
#else
        *where = addr;
#endif
        break;
    default:
        ESP_LOGE(TAG, "reloc %d not supported", ELF_R_TYPE(rela->info));
        return -EINVAL;
    }

    return 0;
}
