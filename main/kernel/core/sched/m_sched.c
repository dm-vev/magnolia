#include <string.h>

#include "kernel/core/sched/m_sched.h"
static StaticSemaphore_t g_sched_registry_lock_storage;
static SemaphoreHandle_t g_sched_registry_lock;
static m_sched_task_metadata_t *g_task_registry_head;
static m_sched_task_id_t g_next_task_id = 1;
static m_sched_worker_hooks_t g_worker_hooks;

typedef struct {
    m_sched_task_metadata_t *meta;
    TaskFunction_t entry;
    void *arg;
} m_sched_internal_task_entry_t;

static inline void m_sched_lock_registry(void)
{
    if (g_sched_registry_lock) {
        xSemaphoreTake(g_sched_registry_lock, portMAX_DELAY);
    }
}

static inline void m_sched_unlock_registry(void)
{
    if (g_sched_registry_lock) {
        xSemaphoreGive(g_sched_registry_lock);
    }
}

static m_sched_task_metadata_t *m_sched_find_metadata_by_id_nolock(
        m_sched_task_id_t id)
{
    m_sched_task_metadata_t *current = g_task_registry_head;
    while (current != NULL && current->id != id) {
        current = current->next;
    }
    return current;
}

static m_sched_task_metadata_t *m_sched_find_metadata_by_handle_nolock(
        TaskHandle_t handle)
{
    m_sched_task_metadata_t *current = g_task_registry_head;
    while (current != NULL && current->handle != handle) {
        current = current->next;
    }
    return current;
}

static void m_sched_metadata_finalize(m_sched_task_metadata_t *meta)
{
    if (meta == NULL) {
        return;
    }

    m_sched_lock_registry();
    if (meta->finalized) {
        m_sched_unlock_registry();
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

    m_sched_unlock_registry();
    vPortFree(meta);
}

static void m_sched_metadata_set_state(m_sched_task_metadata_t *meta,
                                       m_sched_task_state_t state)
{
    if (meta == NULL) {
        return;
    }

    m_sched_lock_registry();
    meta->state = state;
    m_sched_unlock_registry();
}

static void m_sched_metadata_set_wait_reason(m_sched_task_metadata_t *meta,
                                             m_sched_wait_reason_t reason)
{
    if (meta == NULL) {
        return;
    }

    m_sched_lock_registry();
    meta->wait_reason = reason;
    m_sched_unlock_registry();
}

static void m_sched_notify_worker_start(m_sched_task_metadata_t *meta)
{
    if (meta == NULL || !(meta->creation_flags & M_SCHED_TASK_FLAG_WORKER)) {
        return;
    }

    m_sched_worker_hooks_t hooks;
    m_sched_lock_registry();
    hooks = g_worker_hooks;
    m_sched_unlock_registry();

    if (hooks.on_worker_start) {
        hooks.on_worker_start(meta->id, meta, hooks.user_data);
    }
}

static void m_sched_notify_worker_stop(m_sched_task_metadata_t *meta)
{
    if (meta == NULL || !(meta->creation_flags & M_SCHED_TASK_FLAG_WORKER)) {
        return;
    }

    m_sched_worker_hooks_t hooks;
    m_sched_lock_registry();
    hooks = g_worker_hooks;
    m_sched_unlock_registry();

    if (hooks.on_worker_stop) {
        hooks.on_worker_stop(meta->id, meta, hooks.user_data);
    }
}

static void m_sched_task_wrapper(void *arg)
{
    m_sched_internal_task_entry_t *entry = arg;
    if (entry == NULL || entry->meta == NULL || entry->entry == NULL) {
        vPortFree(entry);
        vTaskDelete(NULL);
        return;
    }

    m_sched_task_metadata_t *meta = entry->meta;
    meta->handle = xTaskGetCurrentTaskHandle();
    m_sched_metadata_set_state(meta, M_SCHED_STATE_RUNNING);
    m_sched_notify_worker_start(meta);

    entry->entry(entry->arg);

    m_sched_notify_worker_stop(meta);
    m_sched_metadata_set_state(meta, M_SCHED_STATE_TERMINATED);
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

    m_sched_lock_registry();
    meta->id = g_next_task_id;
    g_next_task_id++;
    if (g_next_task_id == M_SCHED_TASK_ID_INVALID) {
        g_next_task_id = 1;
    }
    meta->next = g_task_registry_head;
    g_task_registry_head = meta;
    m_sched_unlock_registry();

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

    m_sched_lock_registry();
    meta = m_sched_find_metadata_by_id_nolock(id);
    if (meta == NULL) {
        m_sched_unlock_registry();
        return M_SCHED_ERR_NOT_FOUND;
    }

    handle = meta->handle;
    meta->state = M_SCHED_STATE_TERMINATED;
    meta->wait_reason = M_SCHED_WAIT_REASON_NONE;
    m_sched_unlock_registry();

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

    m_sched_lock_registry();
    meta = m_sched_find_metadata_by_id_nolock(id);
    if (meta == NULL || meta->handle == NULL) {
        m_sched_unlock_registry();
        return M_SCHED_ERR_NOT_FOUND;
    }
    handle = meta->handle;
    meta->state = M_SCHED_STATE_SUSPENDED;
    meta->wait_reason = M_SCHED_WAIT_REASON_NONE;
    m_sched_unlock_registry();

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

    m_sched_lock_registry();
    meta = m_sched_find_metadata_by_id_nolock(id);
    if (meta == NULL || meta->handle == NULL) {
        m_sched_unlock_registry();
        return M_SCHED_ERR_NOT_FOUND;
    }
    handle = meta->handle;
    meta->state = M_SCHED_STATE_READY;
    meta->wait_reason = M_SCHED_WAIT_REASON_NONE;
    m_sched_unlock_registry();

    vTaskResume(handle);
    return M_SCHED_OK;
}

void m_sched_task_yield(void)
{
    TaskHandle_t self = xTaskGetCurrentTaskHandle();
    m_sched_lock_registry();
    m_sched_task_metadata_t *meta = m_sched_find_metadata_by_handle_nolock(self);
    if (meta != NULL) {
        meta->state = M_SCHED_STATE_READY;
    }
    m_sched_unlock_registry();
    taskYIELD();
}

m_sched_wait_result_t m_sched_sleep_ms(uint32_t milliseconds)
{
    m_sched_wait_context_t context = {0};
    m_sched_wait_context_prepare_with_reason(&context,
                                            M_SCHED_WAIT_REASON_DELAY);
    uint64_t microseconds = (uint64_t)milliseconds * 1000ULL;
    m_timer_deadline_t deadline = m_timer_deadline_from_relative(microseconds);
    return m_sched_wait_block(&context, &deadline);
}

m_sched_wait_result_t m_sched_sleep_until(m_timer_time_t deadline)
{
    m_sched_wait_context_t context = {0};
    m_sched_wait_context_prepare_with_reason(&context,
                                            M_SCHED_WAIT_REASON_DELAY);
    m_timer_deadline_t target = {.target = deadline,
                                 .infinite = (deadline == M_TIMER_TIMEOUT_FOREVER)};
    return m_sched_wait_block(&context, &target);
}

void m_sched_worker_hooks_register(const m_sched_worker_hooks_t *hooks)
{
    m_sched_lock_registry();
    if (hooks == NULL) {
        memset(&g_worker_hooks, 0, sizeof(g_worker_hooks));
        m_sched_unlock_registry();
        return;
    }

    g_worker_hooks = *hooks;
    m_sched_unlock_registry();
}

void m_sched_wait_context_prepare(m_sched_wait_context_t *ctx)
{
    m_sched_wait_context_prepare_with_reason(ctx, M_SCHED_WAIT_REASON_EVENT);
}

void m_sched_wait_context_prepare_with_reason(m_sched_wait_context_t *ctx,
                                               m_sched_wait_reason_t reason)
{
    if (ctx == NULL) {
        return;
    }

    if (!ctx->initialized) {
        ctx->semaphore = xSemaphoreCreateBinaryStatic(&ctx->storage);
        ctx->initialized = (ctx->semaphore != NULL);
    }

    ctx->task = xTaskGetCurrentTaskHandle();
    ctx->reason = reason;
    ctx->armed = true;
    ctx->result = M_SCHED_WAIT_RESULT_OK;
    ctx->owner = NULL;

    m_sched_lock_registry();
    ctx->owner = m_sched_find_metadata_by_handle_nolock(ctx->task);
    m_sched_unlock_registry();
}

m_sched_wait_result_t m_sched_wait_block(m_sched_wait_context_t *ctx,
                                          const m_timer_deadline_t *deadline)
{
    if (ctx == NULL || ctx->semaphore == NULL) {
        return M_SCHED_WAIT_RESULT_SHUTDOWN;
    }

    if (ctx->owner != NULL) {
        m_sched_metadata_set_wait_reason(ctx->owner, ctx->reason);
        m_sched_metadata_set_state(ctx->owner, M_SCHED_STATE_WAITING);
    }

    m_timer_deadline_t infinite = {.infinite = true, .target = 0};
    const m_timer_deadline_t *use_deadline =
            deadline ? deadline : &infinite;

    TickType_t ticks = m_timer_deadline_to_ticks(use_deadline);
    BaseType_t taken = xSemaphoreTake(ctx->semaphore, ticks);

    ctx->armed = false;
    if (ctx->owner != NULL) {
        m_sched_metadata_set_wait_reason(ctx->owner, M_SCHED_WAIT_REASON_NONE);
        m_sched_metadata_set_state(ctx->owner, M_SCHED_STATE_READY);
    }

    if (taken == pdTRUE) {
        return ctx->result;
    }

    ctx->result = (ctx->reason == M_SCHED_WAIT_REASON_DELAY)
                      ? M_SCHED_WAIT_RESULT_OK
                      : M_SCHED_WAIT_RESULT_TIMEOUT;
    return ctx->result;
}

void m_sched_wait_wake(m_sched_wait_context_t *ctx,
                       m_sched_wait_result_t result)
{
    if (ctx == NULL || ctx->semaphore == NULL) {
        return;
    }

    ctx->result = result;
    if (!ctx->armed) {
        return;
    }

    ctx->armed = false;
    xSemaphoreGive(ctx->semaphore);
}

size_t m_sched_task_snapshot(m_sched_task_diag_entry_t *buffer, size_t capacity)
{
    if (buffer == NULL || capacity == 0) {
        return 0;
    }

    m_sched_lock_registry();
    m_sched_task_metadata_t *current = g_task_registry_head;
    size_t count = 0;

    while (current != NULL && count < capacity) {
        buffer[count].id = current->id;
        strncpy(buffer[count].name, current->name, configMAX_TASK_NAME_LEN);
        buffer[count].name[configMAX_TASK_NAME_LEN - 1] = '\0';
        strncpy(buffer[count].tag, current->tag, M_SCHED_TASK_TAG_MAX_LEN);
        buffer[count].tag[M_SCHED_TASK_TAG_MAX_LEN - 1] = '\0';
        buffer[count].state = current->state;
        buffer[count].wait_reason = current->wait_reason;
        current = current->next;
        count++;
    }

    m_sched_unlock_registry();
    return count;
}

bool m_sched_task_metadata_get(m_sched_task_id_t id,
                               m_sched_task_metadata_t *out)
{
    if (id == M_SCHED_TASK_ID_INVALID || out == NULL) {
        return false;
    }

    m_sched_lock_registry();
    m_sched_task_metadata_t *meta = m_sched_find_metadata_by_id_nolock(id);
    if (meta == NULL) {
        m_sched_unlock_registry();
        return false;
    }

    *out = *meta;
    out->next = NULL;
    m_sched_unlock_registry();
    return true;
}

bool m_sched_task_id_is_valid(m_sched_task_id_t id)
{
    if (id == M_SCHED_TASK_ID_INVALID) {
        return false;
    }

    m_sched_lock_registry();
    bool found = m_sched_find_metadata_by_id_nolock(id) != NULL;
    m_sched_unlock_registry();
    return found;
}
