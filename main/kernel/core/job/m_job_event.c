/**
 * @file        kernel/core/job/m_job_event.c
 * @brief       Job lifecycle notification helpers.
 */

#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"

#include "kernel/core/job/m_job_event.h"

typedef struct job_destroy_listener {
    m_job_destroy_callback_t callback;
    void *user_data;
    struct job_destroy_listener *next;
} job_destroy_listener_t;

static portMUX_TYPE g_job_destroy_list_lock =
        (portMUX_TYPE)portMUX_INITIALIZER_UNLOCKED;
static job_destroy_listener_t *g_destroy_list;

m_job_error_t m_job_subscribe_destroy(m_job_destroy_callback_t callback,
                                      void *user_data)
{
    if (callback == NULL) {
        return M_JOB_ERR_INVALID_PARAM;
    }

    job_destroy_listener_t *entry = pvPortMalloc(sizeof(*entry));
    if (entry == NULL) {
        return M_JOB_ERR_NO_MEMORY;
    }

    entry->callback = callback;
    entry->user_data = user_data;

    portENTER_CRITICAL(&g_job_destroy_list_lock);
    entry->next = g_destroy_list;
    g_destroy_list = entry;
    portEXIT_CRITICAL(&g_job_destroy_list_lock);

    return M_JOB_OK;
}

void _m_job_notify_destroyed(m_job_id_t job)
{
    if (job == NULL) {
        return;
    }

    portENTER_CRITICAL(&g_job_destroy_list_lock);
    job_destroy_listener_t *iter = g_destroy_list;
    while (iter != NULL) {
        m_job_destroy_callback_t callback = iter->callback;
        void *user_data = iter->user_data;
        job_destroy_listener_t *next = iter->next;
        portEXIT_CRITICAL(&g_job_destroy_list_lock);

        callback(job, user_data);

        portENTER_CRITICAL(&g_job_destroy_list_lock);
        iter = next;
    }
    portEXIT_CRITICAL(&g_job_destroy_list_lock);
}
