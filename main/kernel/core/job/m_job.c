#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include "kernel/core/job/m_job.h"

static const char *TAG = "m_job";

typedef struct {
    m_job_handler_t handler;
    void *data;
} m_job_t;

typedef struct m_job_submit_wait_node {
    m_sched_wait_context_t ctx;
    struct m_job_submit_wait_node *next;
    bool linked;
} m_job_submit_wait_node_t;

typedef struct m_job_worker {
    m_job_queue_t *queue;
    m_sched_wait_context_t wait;
    struct m_job_worker *next_waiter;
    struct m_job_worker *prev_waiter;
    bool waiting;
    m_sched_task_id_t task_id;
} m_job_worker_t;

struct m_job_queue {
    char name[M_JOB_QUEUE_NAME_MAX_LEN];
    size_t capacity;
    size_t head;
    size_t tail;
    size_t count;
    m_job_t *ring;
    m_job_worker_t *workers;
    size_t worker_count;
    SemaphoreHandle_t lock;
    StaticSemaphore_t lock_storage;
    m_job_worker_t *worker_waiters_head;
    m_job_worker_t *worker_waiters_tail;
    m_job_submit_wait_node_t *submit_waiters_head;
    m_job_submit_wait_node_t *submit_waiters_tail;
    m_job_stats_t stats;
    bool destroyed;
    bool shutdown_requested;
    bool debug;
    size_t active_workers;
};

static inline void m_job_lock(m_job_queue_t *queue)
{
    xSemaphoreTake(queue->lock, portMAX_DELAY);
}

static inline void m_job_unlock(m_job_queue_t *queue)
{
    xSemaphoreGive(queue->lock);
}

static void m_job_worker_wait_append_locked(m_job_queue_t *queue,
                                            m_job_worker_t *worker)
{
    worker->next_waiter = NULL;
    worker->prev_waiter = queue->worker_waiters_tail;
    worker->waiting = true;

    if (queue->worker_waiters_tail) {
        queue->worker_waiters_tail->next_waiter = worker;
    } else {
        queue->worker_waiters_head = worker;
    }

    queue->worker_waiters_tail = worker;
}

static void m_job_worker_wait_remove_locked(m_job_queue_t *queue,
                                            m_job_worker_t *worker)
{
    if (worker == NULL || !worker->waiting) {
        return;
    }

    if (worker->prev_waiter) {
        worker->prev_waiter->next_waiter = worker->next_waiter;
    } else {
        queue->worker_waiters_head = worker->next_waiter;
    }

    if (worker->next_waiter) {
        worker->next_waiter->prev_waiter = worker->prev_waiter;
    } else {
        queue->worker_waiters_tail = worker->prev_waiter;
    }

    worker->next_waiter = NULL;
    worker->prev_waiter = NULL;
    worker->waiting = false;
}

static void m_job_wake_worker_locked(m_job_queue_t *queue)
{
    m_job_worker_t *worker = queue->worker_waiters_head;
    if (worker == NULL) {
        return;
    }

    m_job_worker_wait_remove_locked(queue, worker);
    m_sched_wait_wake(&worker->wait, M_SCHED_WAIT_RESULT_OK);
}

static void m_job_wake_submitter_locked(m_job_queue_t *queue)
{
    if (queue->submit_waiters_head == NULL) {
        return;
    }

    m_job_submit_wait_node_t *node = queue->submit_waiters_head;
    queue->submit_waiters_head = node->next;
    if (queue->submit_waiters_head == NULL) {
        queue->submit_waiters_tail = NULL;
    }

    node->next = NULL;
    node->linked = false;
    m_sched_wait_wake(&node->ctx, M_SCHED_WAIT_RESULT_OK);
}

static void m_job_wake_all_workers_locked(m_job_queue_t *queue,
                                          m_sched_wait_result_t result)
{
    m_job_worker_t *worker = queue->worker_waiters_head;
    while (worker != NULL) {
        m_job_worker_t *next = worker->next_waiter;
        worker->next_waiter = NULL;
        worker->prev_waiter = NULL;
        worker->waiting = false;
        m_sched_wait_wake(&worker->wait, result);
        worker = next;
    }

    queue->worker_waiters_head = NULL;
    queue->worker_waiters_tail = NULL;
}

static void m_job_submit_wait_remove_locked(m_job_queue_t *queue,
                                            m_job_submit_wait_node_t *node)
{
    if (node == NULL || !node->linked) {
        return;
    }

    m_job_submit_wait_node_t *prev = NULL;
    m_job_submit_wait_node_t *current = queue->submit_waiters_head;
    while (current != NULL && current != node) {
        prev = current;
        current = current->next;
    }

    if (current == NULL) {
        return;
    }

    if (prev != NULL) {
        prev->next = current->next;
    } else {
        queue->submit_waiters_head = current->next;
    }

    if (queue->submit_waiters_tail == current) {
        queue->submit_waiters_tail = prev;
    }

    current->next = NULL;
    current->linked = false;
}

static void m_job_wake_all_submitters_locked(m_job_queue_t *queue,
                                             m_sched_wait_result_t result)
{
    m_job_submit_wait_node_t *node = queue->submit_waiters_head;
    while (node != NULL) {
        m_job_submit_wait_node_t *next = node->next;
        node->next = NULL;
        node->linked = false;
        m_sched_wait_wake(&node->ctx, result);
        node = next;
    }

    queue->submit_waiters_head = NULL;
    queue->submit_waiters_tail = NULL;
}

static void m_job_enqueue_job_locked(m_job_queue_t *queue, m_job_t job)
{
    queue->ring[queue->tail] = job;
    queue->tail = (queue->tail + 1) % queue->capacity;
    queue->count++;
    m_job_wake_worker_locked(queue);
}

static void m_job_worker_hook_start(m_sched_task_id_t id,
                                    m_sched_task_metadata_t *meta,
                                    void *user_data)
{
    (void)id;
    (void)user_data;

    m_job_queue_t *queue = (meta ? (m_job_queue_t *)meta->user_data : NULL);
    if (queue == NULL) {
        return;
    }

    m_job_lock(queue);
    ++queue->active_workers;
    if (queue->debug) {
        ESP_LOGD(TAG,
                 "worker %u started (active=%u)",
                 (unsigned)meta->id,
                 (unsigned)queue->active_workers);
    }
    m_job_unlock(queue);
}

static void m_job_worker_hook_stop(m_sched_task_id_t id,
                                   m_sched_task_metadata_t *meta,
                                   void *user_data)
{
    (void)id;
    (void)user_data;

    m_job_queue_t *queue = (meta ? (m_job_queue_t *)meta->user_data : NULL);
    if (queue == NULL) {
        return;
    }

    m_job_lock(queue);
    if (queue->active_workers > 0) {
        --queue->active_workers;
    }
    if (queue->debug) {
        ESP_LOGD(TAG,
                 "worker %u stopped (active=%u)",
                 (unsigned)meta->id,
                 (unsigned)queue->active_workers);
    }
    m_job_unlock(queue);
}

static void m_job_register_scheduler_hooks(void)
{
    static bool registered = false;
    if (registered) {
        return;
    }

    m_sched_worker_hooks_t hooks = {
        .on_worker_start = m_job_worker_hook_start,
        .on_worker_stop = m_job_worker_hook_stop,
    };
    m_sched_worker_hooks_register(&hooks);
    registered = true;
}

static m_job_error_t m_job_wait_for_space(m_job_queue_t *queue,
                                          const m_timer_deadline_t *deadline)
{
    m_job_submit_wait_node_t *node = NULL;
    while (queue->count >= queue->capacity) {
        if (queue->destroyed) {
            m_job_unlock(queue);
            return M_JOB_ERR_DESTROYED;
        }

        if (queue->shutdown_requested) {
            m_job_unlock(queue);
            return M_JOB_ERR_SHUTDOWN;
        }

        if (node == NULL) {
            node = pvPortMalloc(sizeof(*node));
            if (node == NULL) {
                m_job_unlock(queue);
                return M_JOB_ERR_NO_MEMORY;
            }
            memset(node, 0, sizeof(*node));
            m_sched_wait_context_prepare_with_reason(&node->ctx,
                                                    M_SCHED_WAIT_REASON_JOB);
            node->linked = false;
        }

        node->linked = true;
        node->next = NULL;
        if (queue->submit_waiters_tail) {
            queue->submit_waiters_tail->next = node;
        } else {
            queue->submit_waiters_head = node;
        }
        queue->submit_waiters_tail = node;

        m_job_unlock(queue);
        m_sched_wait_result_t wait_res = m_sched_wait_block(&node->ctx, deadline);
        m_job_lock(queue);

        if (node->linked) {
            m_job_submit_wait_remove_locked(queue, node);
        }

        if (wait_res != M_SCHED_WAIT_RESULT_OK) {
            vPortFree(node);
            queue->stats.dropped++;
            m_job_error_t err = (wait_res == M_SCHED_WAIT_RESULT_TIMEOUT)
                                ? M_JOB_ERR_TIMEOUT
                                : (wait_res == M_SCHED_WAIT_RESULT_OBJECT_DESTROYED)
                                        ? M_JOB_ERR_DESTROYED
                                        : M_JOB_ERR_SHUTDOWN;
            m_job_unlock(queue);
            return err;
        }

        vPortFree(node);
        node = NULL;
    }

    return M_JOB_OK;
}

static m_job_error_t m_job_queue_take(m_job_queue_t *queue,
                                      m_job_t *out,
                                      m_job_worker_t *worker)
{
    m_job_lock(queue);
    while (queue->count == 0) {
        if (queue->destroyed) {
            m_job_unlock(queue);
            return M_JOB_ERR_DESTROYED;
        }

        if (queue->shutdown_requested) {
            m_job_unlock(queue);
            return M_JOB_ERR_SHUTDOWN;
        }

        m_sched_wait_context_prepare_with_reason(&worker->wait,
                                                M_SCHED_WAIT_REASON_JOB);
        m_job_worker_wait_append_locked(queue, worker);
        m_job_unlock(queue);

        m_sched_wait_result_t wait_res = m_sched_wait_block(&worker->wait, NULL);

        m_job_lock(queue);
        m_job_worker_wait_remove_locked(queue, worker);
        if (wait_res != M_SCHED_WAIT_RESULT_OK) {
            m_job_unlock(queue);
            return (wait_res == M_SCHED_WAIT_RESULT_OBJECT_DESTROYED)
                           ? M_JOB_ERR_DESTROYED
                           : M_JOB_ERR_SHUTDOWN;
        }
    }

    *out = queue->ring[queue->head];
    queue->head = (queue->head + 1) % queue->capacity;
    queue->count--;
    m_job_wake_submitter_locked(queue);
    m_job_unlock(queue);
    return M_JOB_OK;
}

static void m_job_worker_entry(void *arg)
{
    m_job_worker_t *worker = arg;
    if (worker == NULL || worker->queue == NULL) {
        vTaskDelete(NULL);
        return;
    }

    m_job_queue_t *queue = worker->queue;
    while (true) {
        m_job_t job;
        m_job_error_t err = m_job_queue_take(queue, &job, worker);
        if (err != M_JOB_OK) {
            break;
        }

        bool success = job.handler(job.data);
        m_job_lock(queue);
        queue->stats.executed++;
        if (!success) {
            queue->stats.failed++;
        }
        m_job_unlock(queue);
    }

    vTaskDelete(NULL);
}

m_job_queue_t *m_job_queue_create(const m_job_queue_config_t *config)
{
    if (config == NULL || config->capacity == 0 || config->worker_count == 0
        || config->name == NULL) {
        return NULL;
    }

    m_job_queue_t *queue = pvPortMalloc(sizeof(*queue));
    if (queue == NULL) {
        return NULL;
    }
    memset(queue, 0, sizeof(*queue));
    strncpy(queue->name, config->name, M_JOB_QUEUE_NAME_MAX_LEN);
    queue->name[M_JOB_QUEUE_NAME_MAX_LEN - 1] = '\0';
    queue->capacity = config->capacity;
    queue->worker_count = config->worker_count;
    queue->debug = config->debug_log;

    queue->ring = pvPortMalloc(sizeof(m_job_t) * queue->capacity);
    if (queue->ring == NULL) {
        vPortFree(queue);
        return NULL;
    }

    queue->workers =
            pvPortMalloc(sizeof(m_job_worker_t) * queue->worker_count);
    if (queue->workers == NULL) {
        vPortFree(queue->ring);
        vPortFree(queue);
        return NULL;
    }

    queue->lock =
            xSemaphoreCreateMutexStatic(&queue->lock_storage);
    if (queue->lock == NULL) {
        vPortFree(queue->workers);
        vPortFree(queue->ring);
        vPortFree(queue);
        return NULL;
    }

    m_job_register_scheduler_hooks();

    size_t stack_depth =
            config->stack_depth ? config->stack_depth : configMINIMAL_STACK_SIZE;
    UBaseType_t priority =
            config->priority ? config->priority : (tskIDLE_PRIORITY + 1u);

    size_t max_worker_index = (queue->worker_count == 0)
            ? 0
            : queue->worker_count - 1;
    int worker_index_digits = 1;
    size_t temp_index = max_worker_index;
    while (temp_index >= 10) {
        temp_index /= 10;
        ++worker_index_digits;
    }

    for (size_t i = 0; i < queue->worker_count; ++i) {
        m_job_worker_t *worker = &queue->workers[i];
        worker->queue = queue;
        worker->waiting = false;
        worker->next_waiter = NULL;
        worker->prev_waiter = NULL;
        worker->task_id = M_SCHED_TASK_ID_INVALID;

        char worker_name[configMAX_TASK_NAME_LEN];
        const int suffix_len = 4 + worker_index_digits;
        int prefix_len = (int)sizeof(worker_name) - suffix_len - 1;
        if (prefix_len < 0) {
            prefix_len = 0;
        }
        snprintf(worker_name,
                 sizeof(worker_name),
                 "%.*s-wrk%u",
                 prefix_len,
                 queue->name,
                 (unsigned)i);

        m_sched_task_options_t opts = {
            .name = worker_name,
            .entry = m_job_worker_entry,
            .argument = worker,
            .stack_depth = stack_depth,
            .priority = priority,
            .creation_flags = M_SCHED_TASK_FLAG_WORKER,
            .tag = queue->name,
            .user_data = queue,
            .cpu_affinity = M_SCHED_CPU_AFFINITY_ANY,
        };

        if (m_sched_task_create(&opts, &worker->task_id) != M_SCHED_OK) {
            queue->destroyed = true;
            queue->shutdown_requested = true;
            for (size_t j = 0; j < i; ++j) {
                m_sched_task_destroy(queue->workers[j].task_id);
            }
            vPortFree(queue->workers);
            vPortFree(queue->ring);
            vPortFree(queue);
            return NULL;
        }
    }

    return queue;
}

m_job_error_t m_job_queue_destroy(m_job_queue_t *queue)
{
    if (queue == NULL) {
        return M_JOB_ERR_INVALID_PARAM;
    }

    m_job_lock(queue);
    if (queue->destroyed) {
        m_job_unlock(queue);
        return M_JOB_ERR_DESTROYED;
    }

    queue->shutdown_requested = true;
    queue->destroyed = true;
    m_job_wake_all_submitters_locked(queue, M_SCHED_WAIT_RESULT_OBJECT_DESTROYED);
    m_job_wake_all_workers_locked(queue, M_SCHED_WAIT_RESULT_OBJECT_DESTROYED);
    m_job_unlock(queue);

    while (queue->active_workers > 0) {
        m_sched_sleep_ms(1);
    }

    vPortFree(queue->workers);
    vPortFree(queue->ring);
    vPortFree(queue);
    return M_JOB_OK;
}

m_job_error_t m_job_queue_submit(m_job_queue_t *queue,
                                 m_job_handler_t handler,
                                 void *data)
{
    return m_job_queue_submit_until(queue, handler, data, NULL);
}

m_job_error_t m_job_queue_submit_nowait(m_job_queue_t *queue,
                                        m_job_handler_t handler,
                                        void *data)
{
    if (queue == NULL || handler == NULL) {
        return M_JOB_ERR_INVALID_PARAM;
    }

    m_job_lock(queue);
    if (queue->destroyed || queue->shutdown_requested) {
        m_job_unlock(queue);
        return M_JOB_ERR_DESTROYED;
    }

    if (queue->count >= queue->capacity) {
        queue->stats.dropped++;
        m_job_unlock(queue);
        return M_JOB_ERR_QUEUE_FULL;
    }

    m_job_t job = {.handler = handler, .data = data};
    m_job_enqueue_job_locked(queue, job);
    queue->stats.submitted++;
    m_job_unlock(queue);
    return M_JOB_OK;
}

m_job_error_t m_job_queue_submit_until(m_job_queue_t *queue,
                                       m_job_handler_t handler,
                                       void *data,
                                       const m_timer_deadline_t *deadline)
{
    if (queue == NULL || handler == NULL) {
        return M_JOB_ERR_INVALID_PARAM;
    }

    m_job_error_t result = m_job_wait_for_space(queue, deadline);
    if (result != M_JOB_OK) {
        return result;
    }

    m_job_t job = {.handler = handler, .data = data};
    m_job_enqueue_job_locked(queue, job);
    queue->stats.submitted++;
    m_job_unlock(queue);
    return M_JOB_OK;
}

void m_job_queue_get_info(const m_job_queue_t *queue,
                          m_job_queue_info_t *info)
{
    if (queue == NULL || info == NULL) {
        return;
    }

    m_job_lock((m_job_queue_t *)queue);
    info->capacity = queue->capacity;
    info->depth = queue->count;
    info->worker_count = queue->worker_count;
    info->active_workers = queue->active_workers;
    info->shutdown = queue->shutdown_requested;
    info->destroyed = queue->destroyed;
    m_job_unlock((m_job_queue_t *)queue);
}

void m_job_queue_get_stats(const m_job_queue_t *queue,
                           m_job_stats_t *stats)
{
    if (queue == NULL || stats == NULL) {
        return;
    }

    m_job_lock((m_job_queue_t *)queue);
    *stats = queue->stats;
    m_job_unlock((m_job_queue_t *)queue);
}
