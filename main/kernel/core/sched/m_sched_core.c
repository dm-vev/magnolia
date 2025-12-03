/**
 * @file kernel/core/sched/m_sched_core.c
 * @brief Core scheduler implementation.
 * @details Implements task lifecycle management, registry locking, and the
 *          platform-neutral entry point that marshals Magnolia tasks onto
 *          FreeRTOS.
 */

#include <string.h>

#include "kernel/core/sched/m_sched_core.h"
#include "kernel/core/sched/m_sched_core_internal.h"
#include "kernel/core/sched/m_sched_worker.h"

static StaticSemaphore_t g_sched_registry_lock_storage;
static SemaphoreHandle_t g_sched_registry_lock;
static m_sched_task_metadata_t *g_task_registry_head;
static m_sched_task_id_t g_next_task_id = 1;

/**
 * @brief Context used to hand task metadata into the wrapper.
 */
typedef struct {
    m_sched_task_metadata_t *meta;
    TaskFunction_t entry;
    void *arg;
} m_sched_internal_task_entry_t;

void _m_sched_registry_lock(void)
{
    if (g_sched_registry_lock) {
        xSemaphoreTake(g_sched_registry_lock, portMAX_DELAY);
    }
}

void _m_sched_registry_unlock(void)
{
    if (g_sched_registry_lock) {
        xSemaphoreGive(g_sched_registry_lock);
    }
}

m_sched_task_metadata_t *_m_sched_metadata_find_locked_by_id(
        m_sched_task_id_t id)
{
    m_sched_task_metadata_t *current = g_task_registry_head;
    while (current != NULL && current->id != id) {
        current = current->next;
    }
    return current;
}

m_sched_task_metadata_t *_m_sched_metadata_find_locked_by_handle(
        TaskHandle_t handle)
{
    m_sched_task_metadata_t *current = g_task_registry_head;
    while (current != NULL && current->handle != handle) {
        current = current->next;
    }
    return current;
}

bool _m_sched_registry_iterate(_m_sched_registry_iter_cb callback,
                                void *user_data)
{
    if (callback == NULL) {
        return true;
    }

    bool completed = true;
    _m_sched_registry_lock();
    m_sched_task_metadata_t *current = g_task_registry_head;
    while (current != NULL) {
        if (!callback(current, user_data)) {
            completed = false;
            break;
        }
        current = current->next;
    }
    _m_sched_registry_unlock();
    return completed;
}

/**
 * @brief Finalize metadata when a task leaves the registry.
 */
static void m_sched_metadata_finalize(m_sched_task_metadata_t *meta)
{
    if (meta == NULL) {
        return;
    }

    _m_sched_registry_lock();
    if (meta->finalized) {
        _m_sched_registry_unlock();
        return;
    }

    meta->finalized = true;

    if (g_task_registry_head == meta) {
        g_task_registry_head = meta->next;
    } else {
        m_sched_task_metadata_t *prev = g_task_registry_head;
        while (prev != NULL && prev->next != meta) {
            prev = prev->next;
        }
        if (prev != NULL) {
            prev->next = meta->next;
        }
    }

    _m_sched_registry_unlock();
    vPortFree(meta);
}

/**
 * @brief Insert metadata into the registry and allocate a task id.
 */
static bool m_sched_metadata_assign_id(m_sched_task_metadata_t *meta)
{
    if (meta == NULL) {
        return false;
    }

    _m_sched_registry_lock();
    meta->id = g_next_task_id;
    g_next_task_id++;
    if (g_next_task_id == M_SCHED_TASK_ID_INVALID) {
        g_next_task_id = 1;
    }
    meta->next = g_task_registry_head;
    g_task_registry_head = meta;
    _m_sched_registry_unlock();
    return true;
}

/**
 * @brief FreeRTOS entry wrapper that wires Magnolia metadata and state.
 */
static void m_sched_task_wrapper(void *arg)
{
    if (arg == NULL) {
        vPortFree(arg);
        vTaskDelete(NULL);
        return;
    }

    m_sched_internal_task_entry_t *entry = arg;
    if (entry->meta == NULL || entry->entry == NULL) {
        vPortFree(entry);
        vTaskDelete(NULL);
        return;
    }

    m_sched_task_metadata_t *meta = entry->meta;
    meta->handle = xTaskGetCurrentTaskHandle();
    meta->state = M_SCHED_STATE_RUNNING;
    _m_sched_worker_notify_start(meta);

    entry->entry(entry->arg);

    _m_sched_worker_notify_stop(meta);
    meta->state = M_SCHED_STATE_TERMINATED;
    m_sched_metadata_finalize(meta);
    vPortFree(entry);
    vTaskDelete(NULL);
}

void m_sched_init(void)
{
    if (g_sched_registry_lock == NULL) {
        g_sched_registry_lock =
                xSemaphoreCreateMutexStatic(&g_sched_registry_lock_storage);
    }
}

m_sched_error_t m_sched_task_create(const m_sched_task_options_t *options,
                                    m_sched_task_id_t *out_id)
{
    if (options == NULL || options->name == NULL || options->entry == NULL
        || out_id == NULL) {
        return M_SCHED_ERR_INVALID_PARAM;
    }

    size_t name_len = strlen(options->name);
    if (name_len == 0 || name_len >= configMAX_TASK_NAME_LEN) {
        return M_SCHED_ERR_INVALID_PARAM;
    }

    size_t stack_depth = options->stack_depth
                         ? options->stack_depth
                         : configMINIMAL_STACK_SIZE;
    UBaseType_t priority = options->priority ? options->priority
                                             : (tskIDLE_PRIORITY + 1u);

    m_sched_task_metadata_t *meta = pvPortMalloc(sizeof(*meta));
    if (meta == NULL) {
        return M_SCHED_ERR_NO_MEMORY;
    }

    memset(meta, 0, sizeof(*meta));
    strncpy(meta->name, options->name, configMAX_TASK_NAME_LEN);
    meta->name[configMAX_TASK_NAME_LEN - 1] = '\0';
    if (options->tag) {
        strncpy(meta->tag, options->tag, M_SCHED_TASK_TAG_MAX_LEN);
        meta->tag[M_SCHED_TASK_TAG_MAX_LEN - 1] = '\0';
    }
    meta->creation_flags = options->creation_flags;
    meta->cpu_affinity = options->cpu_affinity;
    meta->user_data = options->user_data;
    meta->state = M_SCHED_STATE_READY;
    meta->wait_reason = M_SCHED_WAIT_REASON_NONE;
    meta->finalized = false;

    m_sched_internal_task_entry_t *entry =
            pvPortMalloc(sizeof(m_sched_internal_task_entry_t));
    if (entry == NULL) {
        vPortFree(meta);
        return M_SCHED_ERR_NO_MEMORY;
    }

    entry->meta = meta;
    entry->entry = options->entry;
    entry->arg = options->argument;

    if (!m_sched_metadata_assign_id(meta)) {
        vPortFree(entry);
        return M_SCHED_ERR_STATE;
    }

    BaseType_t created = pdFAIL;
    TaskHandle_t created_handle = NULL;

#if CONFIG_FREERTOS_SMP
    bool use_affinity = (options->cpu_affinity >= 0);
#else
    (void)options;
    bool use_affinity = false;
#endif

    if (use_affinity) {
#if CONFIG_FREERTOS_SMP
        created = xTaskCreatePinnedToCore(m_sched_task_wrapper,
                                          meta->name,
                                          stack_depth,
                                          entry,
                                          priority,
                                          &created_handle,
                                          options->cpu_affinity);
#endif
    } else {
        created = xTaskCreate(m_sched_task_wrapper,
                              meta->name,
                              stack_depth,
                              entry,
                              priority,
                              &created_handle);
    }

    if (created != pdPASS) {
        m_sched_metadata_finalize(meta);
        vPortFree(entry);
        return M_SCHED_ERR_NO_MEMORY;
    }

    *out_id = meta->id;
    return M_SCHED_OK;
}

m_sched_error_t m_sched_task_destroy(m_sched_task_id_t id)
{
    if (id == M_SCHED_TASK_ID_INVALID) {
        return M_SCHED_ERR_INVALID_PARAM;
    }

    TaskHandle_t handle = NULL;
    m_sched_task_metadata_t *meta = NULL;

    _m_sched_registry_lock();
    meta = _m_sched_metadata_find_locked_by_id(id);
    if (meta == NULL) {
        _m_sched_registry_unlock();
        return M_SCHED_ERR_NOT_FOUND;
    }

    handle = meta->handle;
    meta->state = M_SCHED_STATE_TERMINATED;
    meta->wait_reason = M_SCHED_WAIT_REASON_NONE;
    _m_sched_registry_unlock();

    if (handle != NULL) {
        vTaskDelete(handle);
    }

    m_sched_metadata_finalize(meta);
    return M_SCHED_OK;
}

m_sched_error_t m_sched_task_suspend(m_sched_task_id_t id)
{
    if (id == M_SCHED_TASK_ID_INVALID) {
        return M_SCHED_ERR_INVALID_PARAM;
    }

    TaskHandle_t handle = NULL;
    m_sched_task_metadata_t *meta = NULL;

    _m_sched_registry_lock();
    meta = _m_sched_metadata_find_locked_by_id(id);
    if (meta == NULL || meta->handle == NULL) {
        _m_sched_registry_unlock();
        return M_SCHED_ERR_NOT_FOUND;
    }
    handle = meta->handle;
    meta->state = M_SCHED_STATE_SUSPENDED;
    meta->wait_reason = M_SCHED_WAIT_REASON_NONE;
    _m_sched_registry_unlock();

    vTaskSuspend(handle);
    return M_SCHED_OK;
}

m_sched_error_t m_sched_task_resume(m_sched_task_id_t id)
{
    if (id == M_SCHED_TASK_ID_INVALID) {
        return M_SCHED_ERR_INVALID_PARAM;
    }

    TaskHandle_t handle = NULL;
    m_sched_task_metadata_t *meta = NULL;

    _m_sched_registry_lock();
    meta = _m_sched_metadata_find_locked_by_id(id);
    if (meta == NULL || meta->handle == NULL) {
        _m_sched_registry_unlock();
        return M_SCHED_ERR_NOT_FOUND;
    }
    handle = meta->handle;
    meta->state = M_SCHED_STATE_READY;
    meta->wait_reason = M_SCHED_WAIT_REASON_NONE;
    _m_sched_registry_unlock();

    vTaskResume(handle);
    return M_SCHED_OK;
}

void m_sched_task_yield(void)
{
    TaskHandle_t self = xTaskGetCurrentTaskHandle();
    _m_sched_registry_lock();
    m_sched_task_metadata_t *meta =
            _m_sched_metadata_find_locked_by_handle(self);
    if (meta != NULL) {
        meta->state = M_SCHED_STATE_READY;
    }
    _m_sched_registry_unlock();
    taskYIELD();
}
