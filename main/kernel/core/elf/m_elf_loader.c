/*
 * Magnolia kernel ELF loader (baseline).
 * Autonomous implementation; no esp-elfloader runtime.
 */

#include <string.h>
#include <sys/errno.h>
#include <setjmp.h>
#include <stdint.h>

#include "esp_log.h"
#include "esp_heap_caps.h"
#include "soc/soc_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#if SOC_CACHE_INTERNAL_MEM_VIA_L1CACHE
#include "hal/cache_ll.h"
#endif

#include "kernel/core/elf/m_elf_loader.h"
#include "kernel/core/elf/m_elf_symbol.h"
#include "kernel/core/elf/m_elf_platform.h"
#include "kernel/core/libc/m_libc_compat.h"
#include "kernel/core/memory/m_alloc.h"
#include "kernel/core/vfs/m_vfs.h"
#include "kernel/arch/m_arch.h"

#define stype(_s, _t)               ((_s)->type == (_t))
#define sflags(_s, _f)              (((_s)->flags & (_f)) == (_f))
#define ADDR_OFFSET                 (0x400)

static const char *TAG = "m_elf";

static void m_elf_log_stack_watermark(const char *phase)
{
    UBaseType_t raw = uxTaskGetStackHighWaterMark(NULL);
    size_t bytes = (size_t)raw * sizeof(StackType_t);
    ESP_LOGI(TAG,
             "stack watermark %s: %u (%u bytes, StackType_t=%u)",
             phase,
             (unsigned)raw,
             (unsigned)bytes,
             (unsigned)sizeof(StackType_t));
}

static void m_elf_log_job_heap(const char *phase, job_ctx_t *ctx)
{
    magnolia_alloc_job_stats_t stats;
    m_alloc_get_job_stats(ctx, &stats);
    ESP_LOGI(TAG,
             "job heap %s: used=%u peak=%u cap=%u regions=%u",
             phase,
             (unsigned)stats.used_bytes,
             (unsigned)stats.peak_bytes,
             (unsigned)stats.capacity_bytes,
             (unsigned)stats.region_count);
}

static bool m_elf_range_ok(uint32_t offset, uint32_t size, size_t len)
{
    uint64_t end = (uint64_t)offset + (uint64_t)size;
    return end <= len;
}

uintptr_t m_elf_map_vaddr(m_elf_t *elf, uintptr_t vaddr)
{
    if (elf == NULL) {
        return 0;
    }
    uintptr_t end_match = 0;
    for (uint32_t i = 0; i < elf->map_count; ++i) {
        uintptr_t start = elf->maps[i].vaddr;
        uintptr_t end = start + elf->maps[i].size;
        if (vaddr >= start && vaddr < end) {
            return elf->maps[i].addr + (vaddr - start);
        }
        /*
         * Some toolchains expose boundary symbols (e.g. `_heap_end`, `_end`)
         * that are exactly one-past-the-last byte of a PT_LOAD region. They
         * are valid addresses for comparisons/sizing, even though they are
         * not dereferenceable. Prefer an exact in-range match (handled above)
         * and fall back to the end address only if nothing else matches.
         */
        if (vaddr == end) {
            end_match = elf->maps[i].addr + elf->maps[i].size;
        }
    }
    return end_match;
}

static int m_elf_track_alloc(m_elf_t *elf, void *ptr)
{
    if (elf == NULL || ptr == NULL) {
        return -EINVAL;
    }
    if (elf->alloc_count >= (uint32_t)(sizeof(elf->allocs) / sizeof(elf->allocs[0]))) {
        return -ENOMEM;
    }
    elf->allocs[elf->alloc_count++].ptr = ptr;
    return 0;
}

static int m_elf_track_map(m_elf_t *elf, uintptr_t vaddr, uintptr_t addr, uint32_t size)
{
    if (elf == NULL || addr == 0 || size == 0) {
        return -EINVAL;
    }
    if (elf->map_count >= (uint32_t)(sizeof(elf->maps) / sizeof(elf->maps[0]))) {
        return -ENOMEM;
    }
    elf->maps[elf->map_count].vaddr = vaddr;
    elf->maps[elf->map_count].addr = addr;
    elf->maps[elf->map_count].size = size;
    ++elf->map_count;
    return 0;
}

static int m_elf_validate_ehdr(const elf32_hdr_t *ehdr, size_t len)
{
    if (!ehdr) {
        return -EINVAL;
    }
    if (len && len < sizeof(*ehdr)) {
        ESP_LOGE(TAG, "ELF buffer too small");
        return -EINVAL;
    }

    if (ehdr->ident[0] != 0x7f ||
        ehdr->ident[1] != 'E' ||
        ehdr->ident[2] != 'L' ||
        ehdr->ident[3] != 'F') {
        ESP_LOGE(TAG, "Invalid ELF magic");
        return -EINVAL;
    }
    if (ehdr->ident[4] != 1) {
        ESP_LOGE(TAG, "Unsupported ELF class=%u", (unsigned)ehdr->ident[4]);
        return -ENOTSUP;
    }
    if (ehdr->ident[5] != 1) {
        ESP_LOGE(TAG, "Unsupported ELF endian=%u", (unsigned)ehdr->ident[5]);
        return -ENOTSUP;
    }

    if (ehdr->ehsize < sizeof(*ehdr)) {
        ESP_LOGE(TAG, "Invalid ehsize=%u", (unsigned)ehdr->ehsize);
        return -EINVAL;
    }

    if (ehdr->phnum > 0) {
        if (ehdr->phentsize != sizeof(elf32_phdr_t)) {
            ESP_LOGE(TAG, "Invalid phentsize=%u", (unsigned)ehdr->phentsize);
            return -EINVAL;
        }
        uint64_t end = (uint64_t)ehdr->phoff + (uint64_t)ehdr->phnum * (uint64_t)ehdr->phentsize;
        if (end > len) {
            ESP_LOGE(TAG, "Program headers out of range");
            return -EINVAL;
        }
    }

    if (ehdr->shnum > 0) {
        if (ehdr->shentsize != sizeof(elf32_shdr_t)) {
            ESP_LOGE(TAG, "Invalid shentsize=%u", (unsigned)ehdr->shentsize);
            return -EINVAL;
        }
        uint64_t end = (uint64_t)ehdr->shoff + (uint64_t)ehdr->shnum * (uint64_t)ehdr->shentsize;
        if (end > len) {
            ESP_LOGE(TAG, "Section headers out of range");
            return -EINVAL;
        }
        if (ehdr->shstrndx >= ehdr->shnum) {
            ESP_LOGE(TAG, "Invalid shstrndx=%u", (unsigned)ehdr->shstrndx);
            return -EINVAL;
        }
    }

    return 0;
}

static void m_elf_cleanup_loaded(m_elf_t *elf)
{
    if (!elf) {
        return;
    }

    for (uint32_t i = 0; i < elf->alloc_count; ++i) {
        if (elf->allocs[i].ptr != NULL) {
            m_elf_free(elf, elf->allocs[i].ptr);
            elf->allocs[i].ptr = NULL;
        }
    }
    elf->alloc_count = 0;

    elf->psegment = NULL;
    elf->ptext = NULL;
    elf->pdata = NULL;
    elf->map_count = 0;
    elf->load_bias = 0;
    elf->preinit_array = NULL;
    elf->init_array = NULL;
    elf->fini_array = NULL;
    elf->preinit_count = 0;
    elf->init_count = 0;
    elf->fini_count = 0;
}

static int m_elf_load_phdr_image(m_elf_t *elf, const uint8_t *pbuf, size_t len)
{
    const elf32_hdr_t *ehdr = (const elf32_hdr_t *)pbuf;
    if (ehdr->phnum == 0) {
        return -ENOTSUP;
    }

    const elf32_phdr_t *phdr = (const elf32_phdr_t *)(pbuf + ehdr->phoff);
    uintptr_t lowest_vaddr = UINTPTR_MAX;
    uintptr_t lowest_addr = 0;
    uint32_t loaded = 0;

    for (uint32_t i = 0; i < ehdr->phnum; ++i) {
        if (phdr[i].type != PT_LOAD) {
            continue;
        }

        if (phdr[i].memsz < phdr[i].filesz) {
            return -EINVAL;
        }
        if (!m_elf_range_ok(phdr[i].offset, phdr[i].filesz, len)) {
            return -EINVAL;
        }

        bool exec = (phdr[i].flags & PF_X) != 0;
        void *segment = m_elf_malloc(elf, phdr[i].memsz, exec);
        if (segment == NULL) {
            return -ENOMEM;
        }
        if (m_elf_track_alloc(elf, segment) != 0) {
            m_elf_free(elf, segment);
            return -ENOMEM;
        }

        memset(segment, 0, phdr[i].memsz);
        memcpy(segment, pbuf + phdr[i].offset, phdr[i].filesz);

        if (m_elf_track_map(elf, phdr[i].vaddr, (uintptr_t)segment, phdr[i].memsz) != 0) {
            return -ENOMEM;
        }

        if (phdr[i].vaddr < lowest_vaddr) {
            lowest_vaddr = phdr[i].vaddr;
            lowest_addr = (uintptr_t)segment;
        }

        if (exec && elf->ptext == NULL) {
            elf->ptext = (unsigned char *)segment;
        } else if (!exec && elf->pdata == NULL) {
            elf->pdata = (unsigned char *)segment;
        }
        if (elf->psegment == NULL) {
            elf->psegment = (unsigned char *)segment;
        }
        ++loaded;

        m_arch_cache_flush(segment, phdr[i].memsz);
        m_arch_cache_invalidate(segment, phdr[i].memsz);
    }

    if (loaded == 0 || lowest_vaddr == UINTPTR_MAX) {
        return -ENOTSUP;
    }

    elf->svaddr = (uint32_t)lowest_vaddr;
    elf->load_bias = lowest_addr - lowest_vaddr;
    m_arch_cache_barrier();

    uintptr_t entry = m_elf_map_vaddr(elf, ehdr->entry);
    if (entry == 0) {
        return -EINVAL;
    }
#ifdef CONFIG_ELF_LOADER_CACHE_OFFSET
    entry = m_elf_remap_text(elf, entry);
#endif
    elf->entry = (void *)entry;
    return 0;
}

#if CONFIG_ELF_LOADER_BUS_ADDRESS_MIRROR
static int m_elf_load_section(m_elf_t *elf, const uint8_t *pbuf)
{
    uint32_t size;

    const elf32_hdr_t *ehdr = (const elf32_hdr_t *)pbuf;
    const elf32_shdr_t *shdr = (const elf32_shdr_t *)(pbuf + ehdr->shoff);
    const char *shstrab = (const char *)pbuf + shdr[ehdr->shstrndx].offset;

    for (uint32_t i = 0; i < ehdr->shnum; i++) {
        const char *name = shstrab + shdr[i].name;

        if (stype(&shdr[i], SHT_PROGBITS) && sflags(&shdr[i], SHF_ALLOC)) {
            if (sflags(&shdr[i], SHF_EXECINSTR) && !strcmp(ELF_TEXT, name)) {
                elf->sec[ELF_SEC_TEXT].v_addr  = shdr[i].addr;
                elf->sec[ELF_SEC_TEXT].size    = ELF_ALIGN(shdr[i].size, 4);
                elf->sec[ELF_SEC_TEXT].offset  = shdr[i].offset;
            } else if (sflags(&shdr[i], SHF_WRITE) && !strcmp(ELF_DATA, name)) {
                elf->sec[ELF_SEC_DATA].v_addr  = shdr[i].addr;
                elf->sec[ELF_SEC_DATA].size    = shdr[i].size;
                elf->sec[ELF_SEC_DATA].offset  = shdr[i].offset;
            } else if (!strcmp(ELF_RODATA, name)) {
                elf->sec[ELF_SEC_RODATA].v_addr  = shdr[i].addr;
                elf->sec[ELF_SEC_RODATA].size    = shdr[i].size;
                elf->sec[ELF_SEC_RODATA].offset  = shdr[i].offset;
            } else if (!strcmp(ELF_DATA_REL_RO, name)) {
                elf->sec[ELF_SEC_DRLRO].v_addr  = shdr[i].addr;
                elf->sec[ELF_SEC_DRLRO].size    = shdr[i].size;
                elf->sec[ELF_SEC_DRLRO].offset  = shdr[i].offset;
            }
        } else if (stype(&shdr[i], SHT_NOBITS) &&
                   sflags(&shdr[i], SHF_ALLOC | SHF_WRITE) &&
                   !strcmp(ELF_BSS, name)) {
            elf->sec[ELF_SEC_BSS].v_addr  = shdr[i].addr;
            elf->sec[ELF_SEC_BSS].size    = shdr[i].size;
            elf->sec[ELF_SEC_BSS].offset  = shdr[i].offset;
        }
    }

    if (!elf->sec[ELF_SEC_TEXT].size) {
        return -EINVAL;
    }

    elf->ptext = m_elf_malloc(elf, elf->sec[ELF_SEC_TEXT].size, true);
    if (!elf->ptext) {
        return -ENOMEM;
    }
    if (m_elf_track_alloc(elf, elf->ptext) != 0) {
        m_elf_free(elf, elf->ptext);
        elf->ptext = NULL;
        return -ENOMEM;
    }

    size = ELF_ALIGN(elf->sec[ELF_SEC_DATA].size, 4) +
           ELF_ALIGN(elf->sec[ELF_SEC_RODATA].size, 4) +
           ELF_ALIGN(elf->sec[ELF_SEC_DRLRO].size, 4) +
           ELF_ALIGN(elf->sec[ELF_SEC_BSS].size, 4);
    if (size) {
        elf->pdata = m_elf_malloc(elf, size, false);
        if (!elf->pdata) {
            m_elf_cleanup_loaded(elf);
            return -ENOMEM;
        }
        if (m_elf_track_alloc(elf, elf->pdata) != 0) {
            m_elf_cleanup_loaded(elf);
            return -ENOMEM;
        }
    }

    ESP_LOGI(TAG, "ELF load OK");
    ESP_LOGI(TAG, "ELF image size=0x%x", (unsigned)(elf->sec[ELF_SEC_TEXT].size + size));

    elf->sec[ELF_SEC_TEXT].addr = (Elf32_Addr)elf->ptext;
    memcpy(elf->ptext, pbuf + elf->sec[ELF_SEC_TEXT].offset,
           elf->sec[ELF_SEC_TEXT].size);
    (void)m_elf_track_map(elf,
                          elf->sec[ELF_SEC_TEXT].v_addr,
                          elf->sec[ELF_SEC_TEXT].addr,
                          elf->sec[ELF_SEC_TEXT].size);

    if (size) {
        uint8_t *pdata = elf->pdata;

        if (elf->sec[ELF_SEC_DATA].size) {
            elf->sec[ELF_SEC_DATA].addr = (uint32_t)pdata;
            memcpy(pdata, pbuf + elf->sec[ELF_SEC_DATA].offset,
                   elf->sec[ELF_SEC_DATA].size);
            (void)m_elf_track_map(elf,
                                  elf->sec[ELF_SEC_DATA].v_addr,
                                  elf->sec[ELF_SEC_DATA].addr,
                                  elf->sec[ELF_SEC_DATA].size);
            pdata += ELF_ALIGN(elf->sec[ELF_SEC_DATA].size, 4);
        }

        if (elf->sec[ELF_SEC_RODATA].size) {
            elf->sec[ELF_SEC_RODATA].addr = (uint32_t)pdata;
            memcpy(pdata, pbuf + elf->sec[ELF_SEC_RODATA].offset,
                   elf->sec[ELF_SEC_RODATA].size);
            (void)m_elf_track_map(elf,
                                  elf->sec[ELF_SEC_RODATA].v_addr,
                                  elf->sec[ELF_SEC_RODATA].addr,
                                  elf->sec[ELF_SEC_RODATA].size);
            pdata += ELF_ALIGN(elf->sec[ELF_SEC_RODATA].size, 4);
        }

        if (elf->sec[ELF_SEC_DRLRO].size) {
            elf->sec[ELF_SEC_DRLRO].addr = (uint32_t)pdata;
            memcpy(pdata, pbuf + elf->sec[ELF_SEC_DRLRO].offset,
                   elf->sec[ELF_SEC_DRLRO].size);
            (void)m_elf_track_map(elf,
                                  elf->sec[ELF_SEC_DRLRO].v_addr,
                                  elf->sec[ELF_SEC_DRLRO].addr,
                                  elf->sec[ELF_SEC_DRLRO].size);
            pdata += ELF_ALIGN(elf->sec[ELF_SEC_DRLRO].size, 4);
        }

        if (elf->sec[ELF_SEC_BSS].size) {
            elf->sec[ELF_SEC_BSS].addr = (uint32_t)pdata;
            memset(pdata, 0, elf->sec[ELF_SEC_BSS].size);
            (void)m_elf_track_map(elf,
                                  elf->sec[ELF_SEC_BSS].v_addr,
                                  elf->sec[ELF_SEC_BSS].addr,
                                  elf->sec[ELF_SEC_BSS].size);
        }
    }

    elf->load_bias = (uintptr_t)elf->ptext - elf->sec[ELF_SEC_TEXT].v_addr;

    uintptr_t entry_ptr = m_elf_map_vaddr(elf, ehdr->entry);
#ifdef CONFIG_ELF_LOADER_CACHE_OFFSET
    entry_ptr = m_elf_remap_text(elf, entry_ptr);
#endif
    elf->entry = (void *)entry_ptr;

    return 0;
}
#else
static int m_elf_load_segment(m_elf_t *elf, const uint8_t *pbuf)
{
    uint32_t size;
    bool first_segment = false;
    Elf32_Addr vaddr_s = 0;
    Elf32_Addr vaddr_e = 0;

    const elf32_hdr_t *ehdr = (const elf32_hdr_t *)pbuf;
    const elf32_phdr_t *phdr = (const elf32_phdr_t *)(pbuf + ehdr->phoff);

    for (int i = 0; i < ehdr->phnum; i++) {
        if (phdr[i].type != PT_LOAD) {
            continue;
        }

        if (phdr[i].memsz < phdr[i].filesz) {
            ESP_LOGE(TAG, "Invalid segment[%d], memsz: %d, filesz: %d",
                     i, phdr[i].memsz, phdr[i].filesz);
            return -EINVAL;
        }

        if (!first_segment) {
            vaddr_s = phdr[i].vaddr;
            vaddr_e = phdr[i].vaddr + phdr[i].memsz;
            first_segment = true;
            if (vaddr_e < vaddr_s) {
                ESP_LOGE(TAG, "Invalid segment[%d], vaddr: 0x%x, memsz: %d",
                         i, phdr[i].vaddr, phdr[i].memsz);
                return -EINVAL;
            }
        } else {
            if (phdr[i].vaddr < vaddr_e) {
                ESP_LOGE(TAG, "Invalid segment[%d], overlap, vaddr: 0x%x, vaddr_e: 0x%x",
                         i, phdr[i].vaddr, vaddr_e);
                return -EINVAL;
            }

            if (phdr[i].vaddr > vaddr_e + ADDR_OFFSET) {
                ESP_LOGI(TAG, "Padding before segment[%d], padding: %d",
                         i, phdr[i].vaddr - vaddr_e);
            }

            vaddr_e = phdr[i].vaddr + phdr[i].memsz;
            if (vaddr_e < phdr[i].vaddr) {
                ESP_LOGE(TAG, "Invalid segment[%d], overflow, vaddr: 0x%x, vaddr_e: 0x%x",
                         i, phdr[i].vaddr, vaddr_e);
                return -EINVAL;
            }
        }
    }

    size = vaddr_e - vaddr_s;
    if (size == 0) {
        return -EINVAL;
    }

    elf->svaddr = vaddr_s;
    elf->psegment = m_elf_malloc(elf, size, true);
    if (!elf->psegment) {
        return -ENOMEM;
    }
    if (m_elf_track_alloc(elf, elf->psegment) != 0) {
        m_elf_free(elf, elf->psegment);
        elf->psegment = NULL;
        return -ENOMEM;
    }

    memset(elf->psegment, 0, size);

    ESP_LOGI(TAG, "ELF load OK");
    ESP_LOGI(TAG, "ELF image size=0x%x", (unsigned)size);

    for (int i = 0; i < ehdr->phnum; i++) {
        if (phdr[i].type == PT_LOAD) {
            memcpy(elf->psegment + phdr[i].vaddr - vaddr_s,
                   (uint8_t *)pbuf + phdr[i].offset, phdr[i].filesz);
        }
    }

#if SOC_CACHE_INTERNAL_MEM_VIA_L1CACHE
    cache_ll_writeback_all(CACHE_LL_LEVEL_INT_MEM, CACHE_TYPE_DATA, CACHE_LL_ID_ALL);
#endif

    /*
     * Segment-mode relocation still uses section-based mapping in arch code.
     * Populate a synthetic mapping that covers the whole loaded segment so
     * RELATIVE relocations (e.g. GOT/PLT literals) get rewritten properly.
     */
    elf->sec[ELF_SEC_TEXT].v_addr = vaddr_s;
    elf->sec[ELF_SEC_TEXT].addr = (uintptr_t)elf->psegment;
    elf->sec[ELF_SEC_TEXT].size = size;
    (void)m_elf_track_map(elf, vaddr_s, (uintptr_t)elf->psegment, size);

    elf->load_bias = (uintptr_t)elf->psegment - vaddr_s;
    elf->entry = (void *)m_elf_map_vaddr(elf, ehdr->entry);
    return 0;
}
#endif

int m_elf_init(m_elf_t *elf, job_ctx_t *ctx)
{
    if (!elf) {
        return -EINVAL;
    }
    memset(elf, 0, sizeof(*elf));
    elf->ctx = ctx;
    return 0;
}

int m_elf_relocate(m_elf_t *elf, const uint8_t *pbuf, size_t len)
{
    int ret;
    const elf32_hdr_t *ehdr;
    const elf32_shdr_t *shdr;
    const char *shstrab;

    if (!elf || !pbuf) {
        return -EINVAL;
    }

    ehdr = (const elf32_hdr_t *)pbuf;
    ret = m_elf_validate_ehdr(ehdr, len);
    if (ret) {
        return ret;
    }
    ESP_LOGI(TAG, "ELF found and parsed");

    shdr = (const elf32_shdr_t *)(pbuf + ehdr->shoff);
    if (!m_elf_range_ok(shdr[ehdr->shstrndx].offset,
                        shdr[ehdr->shstrndx].size,
                        len)) {
        return -EINVAL;
    }
    shstrab = (const char *)pbuf + shdr[ehdr->shstrndx].offset;
    uint32_t shstr_size = shdr[ehdr->shstrndx].size;

    /* Prefer program-header based loading (covers GOT/init_array/etc). */
    ret = m_elf_load_phdr_image(elf, pbuf, len);
    if (ret == -ENOTSUP) {
#if CONFIG_ELF_LOADER_BUS_ADDRESS_MIRROR
        ret = m_elf_load_section(elf, pbuf);
#else
        ret = m_elf_load_segment(elf, pbuf);
#endif
    }
    if (ret) {
        ESP_LOGE(TAG, "Error to load ELF, ret=%d", ret);
        m_elf_cleanup_loaded(elf);
        return ret;
    }

    ESP_LOGI(TAG, "ELF entry=%p", elf->entry);

    if (elf->ctx != NULL && jctx_is_cancelled(elf->ctx)) {
        m_elf_cleanup_loaded(elf);
        return -ECANCELED;
    }

    /* Capture init/fini arrays (invoked in m_elf_request after relocations). */
    for (uint32_t i = 0; i < ehdr->shnum; ++i) {
        if (shdr[i].name >= shstr_size) {
            continue;
        }
        const char *name = shstrab + shdr[i].name;
        if (stype(&shdr[i], SHT_PROGBITS) && sflags(&shdr[i], SHF_ALLOC)) {
            if (!strcmp(name, ".preinit_array") && shdr[i].size) {
                elf->preinit_array = (void (**)(void))m_elf_map_vaddr(elf, shdr[i].addr);
                elf->preinit_count = (uint32_t)(shdr[i].size / sizeof(void (*)(void)));
            } else if (!strcmp(name, ".init_array") && shdr[i].size) {
                elf->init_array = (void (**)(void))m_elf_map_vaddr(elf, shdr[i].addr);
                elf->init_count = (uint32_t)(shdr[i].size / sizeof(void (*)(void)));
            } else if (!strcmp(name, ".fini_array") && shdr[i].size) {
                elf->fini_array = (void (**)(void))m_elf_map_vaddr(elf, shdr[i].addr);
                elf->fini_count = (uint32_t)(shdr[i].size / sizeof(void (*)(void)));
            }
        }
    }

    for (uint32_t i = 0; i < ehdr->shnum; i++) {
        if (stype(&shdr[i], SHT_RELA)) {
            uint32_t nr_reloc = shdr[i].size / sizeof(elf32_rela_t);
            const elf32_rela_t *rela = (const elf32_rela_t *)(pbuf + shdr[i].offset);
            const elf32_sym_t *symtab = (const elf32_sym_t *)(pbuf + shdr[shdr[i].link].offset);
            const char *strtab = (const char *)(pbuf + shdr[shdr[shdr[i].link].link].offset);
            uint32_t sym_count = shdr[shdr[i].link].size / sizeof(elf32_sym_t);

            if (!m_elf_range_ok(shdr[i].offset, shdr[i].size, len) ||
                !m_elf_range_ok(shdr[shdr[i].link].offset, shdr[shdr[i].link].size, len) ||
                !m_elf_range_ok(shdr[shdr[shdr[i].link].link].offset,
                                shdr[shdr[shdr[i].link].link].size,
                                len)) {
                m_elf_cleanup_loaded(elf);
                return -EINVAL;
            }

            if (shdr[i].name < shstr_size) {
                ESP_LOGD(TAG, "Section %s has %d relocations", shstrab + shdr[i].name, (int)nr_reloc);
            }

            for (uint32_t r = 0; r < nr_reloc; r++) {
                if ((r & 0x3fu) == 0 && elf->ctx != NULL && jctx_is_cancelled(elf->ctx)) {
                    m_elf_cleanup_loaded(elf);
                    return -ECANCELED;
                }
                uintptr_t addr = 0;
                elf32_rela_t rela_buf;
                memcpy(&rela_buf, &rela[r], sizeof(rela_buf));

                uint32_t sym_index = ELF_R_SYM(rela_buf.info);
                if (sym_index >= sym_count) {
                    m_elf_cleanup_loaded(elf);
                    return -EINVAL;
                }
                const elf32_sym_t *sym = &symtab[sym_index];
                unsigned char sym_type = ELF32_ST_TYPE(sym->info);
                unsigned char reloc_type = ELF_R_TYPE(rela_buf.info);

                if (reloc_type == 0 || reloc_type == 2) {
                    /* NONE/RTLD: nothing to resolve */
                } else if (sym_type == STT_COMMON || sym_type == STT_OBJECT || sym_type == STT_SECTION) {
                    const char *comm_name = strtab + sym->name;
                    if (comm_name[0]) {
                        addr = m_elf_find_sym(comm_name);
                        if (!addr) {
                            ESP_LOGE(TAG, "Can't find common %s", comm_name);
                            m_elf_cleanup_loaded(elf);
                            return -ENOSYS;
                        }
                    }
                } else {
                    const char *sym_name = strtab + sym->name;
                    if (sym_name[0]) {
                        addr = m_elf_find_sym(sym_name);
                    }
                    if (!addr && sym->value) {
                        addr = m_elf_map_vaddr(elf, sym->value);
                    }
                    if (!addr && sym_name[0]) {
                        ESP_LOGE(TAG, "Can't find symbol %s", sym_name);
                        m_elf_cleanup_loaded(elf);
                        return -ENOSYS;
                    }
                }

                ret = m_elf_arch_relocate(elf, &rela_buf, sym, addr);
                if (ret < 0) {
                    m_elf_cleanup_loaded(elf);
                    return ret;
                }
            }
        }
    }

#ifdef CONFIG_ELF_LOADER_LOAD_PSRAM
    m_elf_arch_flush();
#endif

    for (uint32_t i = 0; i < elf->map_count; ++i) {
        m_arch_cache_flush((void *)elf->maps[i].addr, elf->maps[i].size);
        m_arch_cache_invalidate((void *)elf->maps[i].addr, elf->maps[i].size);
    }
    m_arch_cache_barrier();

    return 0;
}

int m_elf_request(m_elf_t *elf, int opt, int argc, char *argv[])
{
    if (!elf || !elf->entry) {
        return -EINVAL;
    }
    (void)opt;
    ESP_LOGI(TAG, "ELF started");
    m_elf_log_stack_watermark("before");
    m_elf_log_job_heap("before", elf->ctx);

    if (elf->ctx != NULL && jctx_is_cancelled(elf->ctx)) {
        return -ECANCELED;
    }

    m_libc_exit_frame_t *exit_frame =
            (m_libc_exit_frame_t *)m_job_alloc(elf->ctx, sizeof(*exit_frame));
    if (exit_frame == NULL) {
        return -ENOMEM;
    }
    memset(exit_frame, 0, sizeof(*exit_frame));
    m_libc_exit_frame_push(exit_frame);

    int rc = 0;
    if (setjmp(exit_frame->env) == 0) {
        if (elf->preinit_array != NULL) {
            for (uint32_t i = 0; i < elf->preinit_count; ++i) {
                void (*fn)(void) = elf->preinit_array[i];
                if (fn != NULL) {
                    fn();
                }
            }
        }
        if (elf->init_array != NULL) {
            for (uint32_t i = 0; i < elf->init_count; ++i) {
                void (*fn)(void) = elf->init_array[i];
                if (fn != NULL) {
                    fn();
                }
            }
        }
        rc = elf->entry(argc, argv);
    } else {
        rc = exit_frame->code;
    }

    if (elf->fini_array != NULL) {
        for (uint32_t i = elf->fini_count; i > 0; --i) {
            void (*fn)(void) = elf->fini_array[i - 1];
            if (fn != NULL) {
                fn();
            }
        }
    }

    m_libc_exit_frame_pop(exit_frame);
    m_job_free(elf->ctx, exit_frame);
    ESP_LOGI(TAG, "ELF finished, rc=%d", rc);
    m_elf_log_stack_watermark("after");
    m_elf_log_job_heap("after", elf->ctx);
    return rc;
}

void m_elf_deinit(m_elf_t *elf)
{
    if (!elf) {
        return;
    }
    m_elf_cleanup_loaded(elf);
}

int m_elf_run_buffer(const uint8_t *pbuf, size_t len, int argc, char *argv[], int *out_rc)
{
    job_ctx_t *ctx = jctx_current();
    m_elf_t *elf = (m_elf_t *)m_job_alloc(ctx, sizeof(*elf));
    if (elf == NULL) {
        return -ENOMEM;
    }

    int ret = m_elf_init(elf, ctx);
    if (ret < 0) {
        m_job_free(ctx, elf);
        return ret;
    }
    ret = m_elf_relocate(elf, pbuf, len);
    if (ret < 0) {
        m_elf_deinit(elf);
        m_job_free(ctx, elf);
        return ret;
    }
    int rc = m_elf_request(elf, 0, argc, argv);
    m_elf_deinit(elf);
    m_job_free(ctx, elf);
    if (out_rc) {
        *out_rc = rc;
    }
    return 0;
}

int m_elf_run_file(const char *path, int argc, char *argv[], int *out_rc)
{
    if (!path) {
        return -EINVAL;
    }

    int fd = -1;
    m_vfs_error_t verr = m_vfs_open(jctx_current_job_id(), path, 0, &fd);
    if (verr != M_VFS_ERR_OK) {
        ESP_LOGE(TAG, "VFS open %s failed err=%d", path, verr);
        return -ENOENT;
    }

    uint8_t *buffer = NULL;
    size_t capacity = 0;
    size_t total = 0;
    uint8_t tmp[256];

    while (true) {
        size_t read_bytes = 0;
        verr = m_vfs_read(jctx_current_job_id(), fd, tmp, sizeof(tmp), &read_bytes);
        if (verr != M_VFS_ERR_OK) {
            ESP_LOGE(TAG, "VFS read %s failed err=%d", path, verr);
            m_vfs_close(jctx_current_job_id(), fd);
            heap_caps_free(buffer);
            return -EIO;
        }
        if (read_bytes == 0) {
            break;
        }

        if (total + read_bytes > capacity) {
            size_t new_capacity = capacity ? capacity * 2 : 1024;
            while (new_capacity < total + read_bytes) {
                new_capacity *= 2;
            }
            /*
             * Read buffer is transient and can exceed a single Magnolia region's
             * max payload (e.g. 8192). Use ESP-IDF heap directly to avoid
             * spurious -ENOMEM from the job allocator.
             */
            uint8_t *new_buf = (uint8_t *)heap_caps_realloc(buffer,
                                                            new_capacity,
                                                            MALLOC_CAP_8BIT);
            if (!new_buf) {
                m_vfs_close(jctx_current_job_id(), fd);
                heap_caps_free(buffer);
                return -ENOMEM;
            }
            buffer = new_buf;
            capacity = new_capacity;
        }

        memcpy(buffer + total, tmp, read_bytes);
        total += read_bytes;
    }

    m_vfs_close(jctx_current_job_id(), fd);

    if (total == 0) {
        heap_caps_free(buffer);
        return -EINVAL;
    }

    ESP_LOGI(TAG, "ELF %s read from VFS size=%u", path, (unsigned)total);
    int rc = 0;
    int ret = m_elf_run_buffer(buffer, total, argc, argv, &rc);
    heap_caps_free(buffer);
    if (ret < 0) {
        return ret;
    }
    if (out_rc) {
        *out_rc = rc;
    }
    return 0;
}
