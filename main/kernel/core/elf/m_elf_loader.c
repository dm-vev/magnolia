/*
 * Magnolia kernel ELF loader (baseline).
 * Autonomous implementation; no esp-elfloader runtime.
 */

#include <string.h>
#include <sys/errno.h>

#include "esp_log.h"
#include "soc/soc_caps.h"

#if SOC_CACHE_INTERNAL_MEM_VIA_L1CACHE
#include "hal/cache_ll.h"
#endif

#include "kernel/core/elf/m_elf_loader.h"
#include "kernel/core/elf/m_elf_symbol.h"
#include "kernel/core/elf/m_elf_platform.h"
#include "kernel/core/vfs/m_vfs.h"

#define stype(_s, _t)               ((_s)->type == (_t))
#define sflags(_s, _f)              (((_s)->flags & (_f)) == (_f))
#define ADDR_OFFSET                 (0x400)

static const char *TAG = "m_elf";

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
    return 0;
}

static void m_elf_cleanup_loaded(m_elf_t *elf)
{
    if (!elf) {
        return;
    }
#if CONFIG_ELF_LOADER_BUS_ADDRESS_MIRROR
    if (elf->pdata) {
        m_elf_free(elf, elf->pdata);
        elf->pdata = NULL;
    }
    if (elf->ptext) {
        m_elf_free(elf, elf->ptext);
        elf->ptext = NULL;
    }
#else
    if (elf->psegment) {
        m_elf_free(elf, elf->psegment);
        elf->psegment = NULL;
    }
#endif
}

#if CONFIG_ELF_LOADER_BUS_ADDRESS_MIRROR
static int m_elf_load_section(m_elf_t *elf, const uint8_t *pbuf)
{
    uint32_t entry;
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

    size = elf->sec[ELF_SEC_DATA].size +
           elf->sec[ELF_SEC_RODATA].size +
           elf->sec[ELF_SEC_BSS].size +
           elf->sec[ELF_SEC_DRLRO].size;
    if (size) {
        elf->pdata = m_elf_malloc(elf, size, false);
        if (!elf->pdata) {
            m_elf_free(elf, elf->ptext);
            elf->ptext = NULL;
            return -ENOMEM;
        }
    }

    ESP_LOGI(TAG, "ELF load OK");
    ESP_LOGI(TAG, "ELF image size=0x%x", (unsigned)(elf->sec[ELF_SEC_TEXT].size + size));

    elf->sec[ELF_SEC_TEXT].addr = (Elf32_Addr)elf->ptext;
    memcpy(elf->ptext, pbuf + elf->sec[ELF_SEC_TEXT].offset,
           elf->sec[ELF_SEC_TEXT].size);

    if (size) {
        uint8_t *pdata = elf->pdata;

        if (elf->sec[ELF_SEC_DATA].size) {
            elf->sec[ELF_SEC_DATA].addr = (uint32_t)pdata;
            memcpy(pdata, pbuf + elf->sec[ELF_SEC_DATA].offset,
                   elf->sec[ELF_SEC_DATA].size);
            pdata += elf->sec[ELF_SEC_DATA].size;
        }

        if (elf->sec[ELF_SEC_RODATA].size) {
            elf->sec[ELF_SEC_RODATA].addr = (uint32_t)pdata;
            memcpy(pdata, pbuf + elf->sec[ELF_SEC_RODATA].offset,
                   elf->sec[ELF_SEC_RODATA].size);
            pdata += elf->sec[ELF_SEC_RODATA].size;
        }

        if (elf->sec[ELF_SEC_DRLRO].size) {
            elf->sec[ELF_SEC_DRLRO].addr = (uint32_t)pdata;
            memcpy(pdata, pbuf + elf->sec[ELF_SEC_DRLRO].offset,
                   elf->sec[ELF_SEC_DRLRO].size);
            pdata += elf->sec[ELF_SEC_DRLRO].size;
        }

        if (elf->sec[ELF_SEC_BSS].size) {
            elf->sec[ELF_SEC_BSS].addr = (uint32_t)pdata;
            memset(pdata, 0, elf->sec[ELF_SEC_BSS].size);
        }
    }

    entry = ehdr->entry + elf->sec[ELF_SEC_TEXT].addr -
            elf->sec[ELF_SEC_TEXT].v_addr;

#ifdef CONFIG_ELF_LOADER_CACHE_OFFSET
    elf->entry = (void *)m_elf_remap_text(elf, (uintptr_t)entry);
#else
    elf->entry = (void *)entry;
#endif

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

    elf->entry = (void *)((uint8_t *)elf->psegment + ehdr->entry - vaddr_s);
    return 0;
}
#endif

static uintptr_t m_elf_map_sym(m_elf_t *elf, uintptr_t sym)
{
    for (int i = 0; i < ELF_SECS; i++) {
        if ((sym >= elf->sec[i].v_addr) &&
            (sym < (elf->sec[i].v_addr + elf->sec[i].size))) {
            return sym - elf->sec[i].v_addr + elf->sec[i].addr;
        }
    }
    return 0;
}

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
    shstrab = (const char *)pbuf + shdr[ehdr->shstrndx].offset;

#if CONFIG_ELF_LOADER_BUS_ADDRESS_MIRROR
    ret = m_elf_load_section(elf, pbuf);
#else
    ret = m_elf_load_segment(elf, pbuf);
#endif
    if (ret) {
        ESP_LOGE(TAG, "Error to load ELF, ret=%d", ret);
        m_elf_cleanup_loaded(elf);
        return ret;
    }

    ESP_LOGI(TAG, "ELF entry=%p", elf->entry);

    for (uint32_t i = 0; i < ehdr->shnum; i++) {
        if (stype(&shdr[i], SHT_RELA)) {
            uint32_t nr_reloc = shdr[i].size / sizeof(elf32_rela_t);
            const elf32_rela_t *rela = (const elf32_rela_t *)(pbuf + shdr[i].offset);
            const elf32_sym_t *symtab = (const elf32_sym_t *)(pbuf + shdr[shdr[i].link].offset);
            const char *strtab = (const char *)(pbuf + shdr[shdr[shdr[i].link].link].offset);

            ESP_LOGD(TAG, "Section %s has %d relocations", shstrab + shdr[i].name, (int)nr_reloc);

            for (uint32_t r = 0; r < nr_reloc; r++) {
                uintptr_t addr = 0;
                elf32_rela_t rela_buf;
                memcpy(&rela_buf, &rela[r], sizeof(rela_buf));

                const elf32_sym_t *sym = &symtab[ELF_R_SYM(rela_buf.info)];
                int type = ELF_R_TYPE(rela_buf.info);

                if (type == STT_COMMON || type == STT_OBJECT || type == STT_SECTION) {
                    const char *comm_name = strtab + sym->name;
                    if (comm_name[0]) {
                        addr = m_elf_find_sym(comm_name);
                        if (!addr) {
                            ESP_LOGE(TAG, "Can't find common %s", comm_name);
                            m_elf_cleanup_loaded(elf);
                            return -ENOSYS;
                        }
                    }
                } else if (type == STT_FILE) {
                    const char *func_name = strtab + sym->name;
                    if (sym->value) {
                        addr = m_elf_map_sym(elf, sym->value);
                    } else {
                        addr = m_elf_find_sym(func_name);
                    }
                    if (!addr) {
                        ESP_LOGE(TAG, "Can't find symbol %s", func_name);
                        m_elf_cleanup_loaded(elf);
                        return -ENOSYS;
                    }
                }

                m_elf_arch_relocate(elf, &rela_buf, sym, addr);
            }
        }
    }

#ifdef CONFIG_ELF_LOADER_LOAD_PSRAM
    m_elf_arch_flush();
#endif

    return 0;
}

int m_elf_request(m_elf_t *elf, int opt, int argc, char *argv[])
{
    if (!elf || !elf->entry) {
        return -EINVAL;
    }
    (void)opt;
    ESP_LOGI(TAG, "ELF started");
    int rc = elf->entry(argc, argv);
    ESP_LOGI(TAG, "ELF finished, rc=%d", rc);
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
    m_elf_t elf;
    int ret = m_elf_init(&elf, jctx_current());
    if (ret < 0) {
        return ret;
    }
    ret = m_elf_relocate(&elf, pbuf, len);
    if (ret < 0) {
        m_elf_deinit(&elf);
        return ret;
    }
    int rc = m_elf_request(&elf, 0, argc, argv);
    m_elf_deinit(&elf);
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
            free(buffer);
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
            uint8_t *new_buf = realloc(buffer, new_capacity);
            if (!new_buf) {
                m_vfs_close(jctx_current_job_id(), fd);
                free(buffer);
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
        free(buffer);
        return -EINVAL;
    }

    ESP_LOGI(TAG, "ELF %s read from VFS size=%u", path, (unsigned)total);
    int rc = 0;
    int ret = m_elf_run_buffer(buffer, total, argc, argv, &rc);
    free(buffer);
    if (ret < 0) {
        return ret;
    }
    if (out_rc) {
        *out_rc = rc;
    }
    return 0;
}
