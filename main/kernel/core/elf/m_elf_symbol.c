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
#if defined(__has_include)
#if __has_include(<getopt.h>)
#include <getopt.h>
#endif
#endif
#include <sys/stat.h>
#include <sys/reent.h>
#include <sys/time.h>
#include <sys/times.h>
#include <ctype.h>
#include <math.h>
#include <time.h>
#include "esp_log.h"

#include "kernel/core/elf/m_elf_app_api.h"
#include "kernel/core/elf/m_elf_loader.h"
#include "kernel/core/elf/m_elf_symbol.h"
#include "kernel/core/libc/m_libc_compat.h"

static const char *TAG = "m_elf_sym";

/* Ensure getopt symbols are declared when exporting them as pointers. */
extern char *optarg;
extern int optind;
extern int opterr;
extern int optopt;
int getopt(int argc, char *const argv[], const char *optstring);
extern char **environ;

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
    /* errno (job-local for ELF) */
    { "__errno", (void *)m_libc___errno },

    /* Termination (unwinds back to ELF loader) */
    { "exit", (void *)m_libc_exit },
    { "_exit", (void *)m_libc__exit },
    { "abort", (void *)m_libc_abort },
#if CONFIG_MAGNOLIA_ELF_EXPORT_NEWLIB
    { "atexit", (void *)m_libc_atexit },
    { "__cxa_atexit", (void *)m_libc___cxa_atexit },
    { "__cxa_finalize", (void *)m_libc___cxa_finalize },
#endif

    /* Magnolia VFS-backed POSIX I/O */
    { "open", (void *)m_libc_open },
    { "close", (void *)m_libc_close },
    { "read", (void *)m_libc_read },
    { "write", (void *)m_libc_write },
    { "lseek", (void *)m_libc_lseek },
    { "ioctl", (void *)m_libc_ioctl },
    { "dup", (void *)m_libc_dup },
    { "dup2", (void *)m_libc_dup2 },
    { "poll", (void *)m_libc_poll },
    { "unlink", (void *)m_libc_unlink },
    { "mkdir", (void *)m_libc_mkdir },
    { "chdir", (void *)m_libc_chdir },
    { "getcwd", (void *)m_libc_getcwd },
    { "stat", (void *)m_libc_stat },
    { "fstat", (void *)m_libc_fstat },
    { "opendir", (void *)m_libc_opendir },
    { "readdir", (void *)m_libc_readdir },
    { "closedir", (void *)m_libc_closedir },
    { "rewinddir", (void *)m_libc_rewinddir },
    { "isatty", (void *)m_libc_isatty },
    { "access", (void *)m_libc_access },
    { "remove", (void *)m_libc_remove },

    /* Time (monotonic-backed) */
    { "clock_gettime", (void *)m_libc_clock_gettime },
    { "gettimeofday", (void *)m_libc_gettimeofday },
    { "time", (void *)m_libc_time },
    { "sleep", (void *)m_libc_sleep },
    { "usleep", (void *)m_libc_usleep },
    { "nanosleep", (void *)m_libc_nanosleep },

    /* Identity (job-mapped) */
    { "getpid", (void *)m_libc_getpid },
    { "getppid", (void *)m_libc_getppid },
    { "getuid", (void *)m_libc_getuid },
    { "getgid", (void *)m_libc_getgid },
    { "geteuid", (void *)m_libc_geteuid },
    { "getegid", (void *)m_libc_getegid },

    /* Memory/string primitives */
    M_ELFSYM_EXPORT(memset),
    M_ELFSYM_EXPORT(memcpy),
    M_ELFSYM_EXPORT(memmove),
    M_ELFSYM_EXPORT(memcmp),
    M_ELFSYM_EXPORT(memchr),
    M_ELFSYM_EXPORT(strlen),
    M_ELFSYM_EXPORT(strnlen),
    M_ELFSYM_EXPORT(strcmp),
    M_ELFSYM_EXPORT(strncmp),
    M_ELFSYM_EXPORT(strcpy),
    M_ELFSYM_EXPORT(strncpy),
    M_ELFSYM_EXPORT(strstr),
    M_ELFSYM_EXPORT(strchr),
    M_ELFSYM_EXPORT(strrchr),
    M_ELFSYM_EXPORT(strcspn),
    M_ELFSYM_EXPORT(strspn),
    M_ELFSYM_EXPORT(strpbrk),
    M_ELFSYM_EXPORT(strtok),
    M_ELFSYM_EXPORT(strtok_r),
    M_ELFSYM_EXPORT(strtol),
    M_ELFSYM_EXPORT(strtoul),
    M_ELFSYM_EXPORT(strtoll),
    M_ELFSYM_EXPORT(strtoull),
    M_ELFSYM_EXPORT(strtod),
    M_ELFSYM_EXPORT(atoi),
    M_ELFSYM_EXPORT(atol),
    M_ELFSYM_EXPORT(atoll),

    /* Diagnostics / errors */
    M_ELFSYM_EXPORT(strerror),
    M_ELFSYM_EXPORT(perror),

    /* Formatting / minimal stdio */
    M_ELFSYM_EXPORT(snprintf),
    M_ELFSYM_EXPORT(vsnprintf),
    M_ELFSYM_EXPORT(printf),
    M_ELFSYM_EXPORT(vprintf),
    M_ELFSYM_EXPORT(puts),
    M_ELFSYM_EXPORT(putchar),

#if CONFIG_MAGNOLIA_ELF_EXPORT_NEWLIB
    /* newlib syscall ABI (used by FILE* and friends) */
    M_ELFSYM_EXPORT(__getreent),
    M_ELFSYM_EXPORT(_impure_ptr),
    { "_malloc_r", (void *)m_libc_malloc_r },
    { "_calloc_r", (void *)m_libc_calloc_r },
    { "_realloc_r", (void *)m_libc_realloc_r },
    { "_free_r", (void *)m_libc_free_r },
    { "_open_r", (void *)m_libc_open_r },
    { "_close_r", (void *)m_libc_close_r },
    { "_read_r", (void *)m_libc_read_r },
    { "_write_r", (void *)m_libc_write_r },
    { "_lseek_r", (void *)m_libc_lseek_r },
    { "_fstat_r", (void *)m_libc_fstat_r },
    { "_stat_r", (void *)m_libc_stat_r },
    { "_isatty_r", (void *)m_libc_isatty_r },
    { "_unlink_r", (void *)m_libc_unlink_r },
    { "_mkdir_r", (void *)m_libc_mkdir_r },
    { "_chdir_r", (void *)m_libc_chdir_r },
    { "_getcwd_r", (void *)m_libc_getcwd_r },
    { "_gettimeofday_r", (void *)m_libc_gettimeofday_r },
    { "_times_r", (void *)m_libc_times_r },
    { "_sbrk_r", (void *)m_libc_sbrk_r },
    { "_kill_r", (void *)m_libc_kill_r },
    { "_getpid_r", (void *)m_libc_getpid_r },
    { "_rename_r", (void *)m_libc_rename_r },
    { "_link_r", (void *)m_libc_link_r },
    { "_rmdir_r", (void *)m_libc_rmdir_r },

    /* FILE* stdio (pulls in newlib stdio implementation) */
    M_ELFSYM_EXPORT(fopen),
    M_ELFSYM_EXPORT(fdopen),
    M_ELFSYM_EXPORT(freopen),
    M_ELFSYM_EXPORT(fclose),
    M_ELFSYM_EXPORT(fread),
    M_ELFSYM_EXPORT(fwrite),
    M_ELFSYM_EXPORT(fflush),
    M_ELFSYM_EXPORT(fseek),
    M_ELFSYM_EXPORT(fseeko),
    M_ELFSYM_EXPORT(ftell),
    M_ELFSYM_EXPORT(ftello),
    M_ELFSYM_EXPORT(rewind),
    M_ELFSYM_EXPORT(fgets),
    M_ELFSYM_EXPORT(fputs),
    M_ELFSYM_EXPORT(fputc),
    M_ELFSYM_EXPORT(fgetc),
    M_ELFSYM_EXPORT(ungetc),
    M_ELFSYM_EXPORT(getc),
    M_ELFSYM_EXPORT(putc),
    M_ELFSYM_EXPORT(fprintf),
    M_ELFSYM_EXPORT(vfprintf),
    M_ELFSYM_EXPORT(sprintf),
    M_ELFSYM_EXPORT(vsprintf),
    M_ELFSYM_EXPORT(getchar),
    /* putchar already exported in the minimal stdio set */

    /* scanf family */
    M_ELFSYM_EXPORT(scanf),
    M_ELFSYM_EXPORT(fscanf),
    M_ELFSYM_EXPORT(sscanf),
    M_ELFSYM_EXPORT(vscanf),
    M_ELFSYM_EXPORT(vfscanf),
    M_ELFSYM_EXPORT(vsscanf),

    /* getopt + environment */
    M_ELFSYM_EXPORT(getopt),
    M_ELFSYM_EXPORT(optarg),
    M_ELFSYM_EXPORT(optind),
    M_ELFSYM_EXPORT(opterr),
    M_ELFSYM_EXPORT(optopt),
    M_ELFSYM_EXPORT(environ),
    M_ELFSYM_EXPORT(getenv),
    M_ELFSYM_EXPORT(setenv),
    M_ELFSYM_EXPORT(unsetenv),
    M_ELFSYM_EXPORT(putenv),

    /* ctype */
    { "_ctype_", (const void *)_ctype_ },
    M_ELFSYM_EXPORT(isalnum),
    M_ELFSYM_EXPORT(isalpha),
    M_ELFSYM_EXPORT(isascii),
    M_ELFSYM_EXPORT(isblank),
    M_ELFSYM_EXPORT(iscntrl),
    M_ELFSYM_EXPORT(isdigit),
    M_ELFSYM_EXPORT(isgraph),
    M_ELFSYM_EXPORT(islower),
    M_ELFSYM_EXPORT(isprint),
    M_ELFSYM_EXPORT(ispunct),
    M_ELFSYM_EXPORT(isspace),
    M_ELFSYM_EXPORT(isupper),
    M_ELFSYM_EXPORT(isxdigit),
    M_ELFSYM_EXPORT(toascii),
    M_ELFSYM_EXPORT(tolower),
    M_ELFSYM_EXPORT(toupper),

    /* time helpers */
    M_ELFSYM_EXPORT(gmtime),
    M_ELFSYM_EXPORT(localtime),
    M_ELFSYM_EXPORT(mktime),
    M_ELFSYM_EXPORT(strftime),

    /* libm (common subset) */
    M_ELFSYM_EXPORT(fabs),
    M_ELFSYM_EXPORT(floor),
    M_ELFSYM_EXPORT(ceil),
    M_ELFSYM_EXPORT(sqrt),
    M_ELFSYM_EXPORT(pow),
    M_ELFSYM_EXPORT(sin),
    M_ELFSYM_EXPORT(cos),
    M_ELFSYM_EXPORT(tan),
#endif

    /* stdlib helpers commonly used by small tools */
    M_ELFSYM_EXPORT(qsort),
    M_ELFSYM_EXPORT(bsearch),
    M_ELFSYM_EXPORT(rand),
    M_ELFSYM_EXPORT(srand),
    M_ELFSYM_EXPORT(strdup),

    /* Memory management (job allocator) */
    { "malloc", (void *)m_libc_malloc },
    { "calloc", (void *)m_libc_calloc },
    { "realloc", (void *)m_libc_realloc },
    { "free", (void *)m_libc_free },

    /* System info */
    M_ELFSYM_EXPORT(m_meminfo),

    /* Magnolia ELF exec helpers (used by /bin/sh and friends) */
    { "m_elf_run_file", (void *)m_elf_run_file },
    { "m_elf_run_buffer", (void *)m_elf_run_buffer },
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
