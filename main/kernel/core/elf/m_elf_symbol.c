/*
 * Kernel ELF symbol registry.
 * Provides a small static export set plus optional dynamic registration.
 */

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/stat.h>
#include "esp_log.h"

#include "kernel/core/elf/m_elf_symbol.h"

static const char *TAG = "m_elf_sym";

struct dyn_m_elfsym {
    const char *name;
    void *sym;
    struct dyn_m_elfsym *next;
};

static struct dyn_m_elfsym *g_dyn_syms;

static uintptr_t _register_symbol_internal(const char *name, void *sym)
{
    if (!name || !sym) {
        return 0;
    }

    struct dyn_m_elfsym *node = malloc(sizeof(*node));
    if (!node) {
        ESP_LOGE(TAG, "symbol alloc failed for %s", name);
        return 0;
    }

    size_t len = strlen(name) + 1;
    char *copy = malloc(len);
    if (!copy) {
        free(node);
        ESP_LOGE(TAG, "symbol name alloc failed for %s", name);
        return 0;
    }
    memcpy(copy, name, len);

    node->name = copy;
    node->sym = sym;
    node->next = g_dyn_syms;
    g_dyn_syms = node;
    return (uintptr_t)sym;
}

uintptr_t m_elf_register_symbol(const char *name, void *sym)
{
    return _register_symbol_internal(name, sym);
}

/* Minimal kernel export set for current ELF tests. Extend via dynamic registry. */
static const struct m_elfsym g_kernel_libc_syms[] = {
    M_ELFSYM_EXPORT(memset),
    M_ELFSYM_EXPORT(memcpy),
    M_ELFSYM_EXPORT(memmove),
    M_ELFSYM_EXPORT(strlen),
    M_ELFSYM_EXPORT(strcmp),
    M_ELFSYM_EXPORT(strncmp),
    M_ELFSYM_EXPORT(strchr),
    M_ELFSYM_EXPORT(strrchr),
    M_ELFSYM_EXPORT(strtol),
    M_ELFSYM_EXPORT(strtod),
    M_ELFSYM_EXPORT(snprintf),
    M_ELFSYM_EXPORT(printf),
    M_ELFSYM_EXPORT(malloc),
    M_ELFSYM_EXPORT(calloc),
    M_ELFSYM_EXPORT(realloc),
    M_ELFSYM_EXPORT(free),
    M_ELFSYM_EXPORT(open),
    M_ELFSYM_EXPORT(read),
    M_ELFSYM_EXPORT(write),
    M_ELFSYM_EXPORT(close),
    M_ELFSYM_EXPORT(lseek),
    M_ELFSYM_EXPORT(stat),
    M_ELFSYM_EXPORT(unlink),
    M_ELFSYM_EXPORT(__errno),
    M_ELFSYM_END
};

uintptr_t m_elf_find_sym(const char *sym_name)
{
    if (!sym_name) {
        return 0;
    }

    const struct m_elfsym *syms = g_kernel_libc_syms;
    while (syms->name) {
        if (!strcmp(syms->name, sym_name)) {
            return (uintptr_t)syms->sym;
        }
        ++syms;
    }

    struct dyn_m_elfsym *node = g_dyn_syms;
    while (node) {
        if (!strcmp(node->name, sym_name)) {
            return (uintptr_t)node->sym;
        }
        node = node->next;
    }

    return 0;
}
