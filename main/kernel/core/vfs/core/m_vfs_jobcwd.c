#include <stddef.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"

#include "kernel/core/vfs/core/m_vfs_jobcwd.h"

typedef struct m_vfs_job_cwd_entry {
    m_job_id_t job;
    char cwd[JOB_CTX_CWD_MAX_LEN];
    struct m_vfs_job_cwd_entry *next;
} m_vfs_job_cwd_entry_t;

static portMUX_TYPE g_vfs_job_cwd_lock =
        (portMUX_TYPE)portMUX_INITIALIZER_UNLOCKED;
static m_vfs_job_cwd_entry_t *g_vfs_job_cwds;

void
m_vfs_job_cwd_init(void)
{
    portENTER_CRITICAL(&g_vfs_job_cwd_lock);
    g_vfs_job_cwds = NULL;
    portEXIT_CRITICAL(&g_vfs_job_cwd_lock);
}

static m_vfs_job_cwd_entry_t *
_m_vfs_job_cwd_find(m_job_id_t job)
{
    m_vfs_job_cwd_entry_t *iter = g_vfs_job_cwds;
    while (iter != NULL) {
        if (iter->job == job) {
            return iter;
        }
        iter = iter->next;
    }
    return NULL;
}

void
m_vfs_job_cwd_update(m_job_id_t job, const char *cwd)
{
    if (job == NULL || cwd == NULL) {
        return;
    }

    portENTER_CRITICAL(&g_vfs_job_cwd_lock);
    m_vfs_job_cwd_entry_t *entry = _m_vfs_job_cwd_find(job);
    if (entry == NULL) {
        entry = pvPortMalloc(sizeof(*entry));
        if (entry == NULL) {
            portEXIT_CRITICAL(&g_vfs_job_cwd_lock);
            return;
        }
        entry->job = job;
        entry->next = g_vfs_job_cwds;
        g_vfs_job_cwds = entry;
    }
    strncpy(entry->cwd, cwd, sizeof(entry->cwd));
    entry->cwd[sizeof(entry->cwd) - 1] = '\0';
    portEXIT_CRITICAL(&g_vfs_job_cwd_lock);
}

void
m_vfs_job_cwd_remove(m_job_id_t job)
{
    if (job == NULL) {
        return;
    }

    portENTER_CRITICAL(&g_vfs_job_cwd_lock);
    m_vfs_job_cwd_entry_t *prev = NULL;
    m_vfs_job_cwd_entry_t *iter = g_vfs_job_cwds;
    while (iter != NULL) {
        if (iter->job == job) {
            if (prev == NULL) {
                g_vfs_job_cwds = iter->next;
            } else {
                prev->next = iter->next;
            }
            vPortFree(iter);
            break;
        }
        prev = iter;
        iter = iter->next;
    }
    portEXIT_CRITICAL(&g_vfs_job_cwd_lock);
}

void
m_vfs_job_cwd_iterate(m_vfs_job_cwd_iter_fn cb, void *user_data)
{
    if (cb == NULL) {
        return;
    }

    portENTER_CRITICAL(&g_vfs_job_cwd_lock);
    m_vfs_job_cwd_entry_t *iter = g_vfs_job_cwds;
    while (iter != NULL) {
        if (!cb(iter->job, iter->cwd, user_data)) {
            break;
        }
        iter = iter->next;
    }
    portEXIT_CRITICAL(&g_vfs_job_cwd_lock);
}
