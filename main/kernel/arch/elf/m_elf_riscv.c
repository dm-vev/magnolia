/*
 * RISC-V relocation support for Magnolia kernel ELF loader.
 */

#include <assert.h>
#include <sys/errno.h>

#include "esp_log.h"

#include "kernel/core/elf/m_elf_loader.h"
#include "kernel/core/elf/m_elf_platform.h"

#define R_RISCV_NONE           0
#define R_RISCV_32             1
#define R_RISCV_RELATIVE       3
#define R_RISCV_JUMP_SLOT      5

static const char *TAG = "m_elf_arch";

int m_elf_arch_relocate(struct m_elf *elf, const elf32_rela_t *rela,
                        const elf32_sym_t *sym, uint32_t addr)
{
    uint32_t *where;
    (void)sym;

    assert(elf && rela);

    where = (uint32_t *)m_elf_map_vaddr((m_elf_t *)elf, rela->offset);
    ESP_LOGD(TAG, "type=%d where=%p addr=0x%x off=0x%x",
             ELF_R_TYPE(rela->info), where, (int)addr, (int)rela->offset);
    if (where == NULL) {
        return -EINVAL;
    }

    switch (ELF_R_TYPE(rela->info)) {
    case R_RISCV_NONE:
        break;
    case R_RISCV_32:
        *where = addr + rela->addend;
        break;
    case R_RISCV_RELATIVE:
        *where = (Elf32_Addr)(((m_elf_t *)elf)->load_bias + (uintptr_t)(intptr_t)rela->addend);
        break;
    case R_RISCV_JUMP_SLOT:
        *where = addr;
        break;
    default:
        ESP_LOGE(TAG, "reloc %d not supported", ELF_R_TYPE(rela->info));
        return -EINVAL;
    }

    return 0;
}
