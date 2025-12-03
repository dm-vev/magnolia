/**
 * @file        m_job_queue.c
 * @brief       Implements the Magnolia job queue operations.
 */

#include <string.h>

#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "kernel/core/job/m_job_queue.h"
#include "kernel/core/job/m_job_worker.h"
#include "kernel/core/job/m_job_core.h"
#include "kernel/core/timer/m_timer.h"
#include "kernel/core/sched/m_sched.h"

/**
 * @brief   Append a worker to the waiter list while holding the queue lock.
 */
static void _m_job_worker_wait_append_locked(m_job_queue_t *queue,
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

/**
 * @brief   Remove a worker from the waiter list and clear its links.
 */
static void _m_job_worker_wait_remove_locked(m_job_queue_t *queue,
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

/**
 * @brief   Wake the next waiting worker while the queue lock is held.
 */
static void _m_job_wake_worker_locked(m_job_queue_t *queue)
{
    m_job_worker_t *worker = queue->worker_waiters_head;
    if (worker == NULL) {
        return;
    }

    _m_job_worker_wait_remove_locked(queue, worker);
    m_sched_wait_wake(&worker->wait, M_SCHED_WAIT_RESULT_OK);
}

/**
 * @brief   Wake all workers linked on the queue and clear their state.
 */
static void _m_job_wake_all_workers_locked(m_job_queue_t *queue,
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

/**
 * @brief   Wake the oldest submitter waiting for queue capacity.
 */
static void _m_job_wake_submitter_locked(m_job_queue_t *queue)
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

/**
 * @brief   Wake all submit waiters with the supplied result.
 */
static void _m_job_wake_all_submitters_locked(m_job_queue_t *queue,
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

/**
 * @brief   Remove a submit waiter node from the queue list while locked.
 */
static void _m_job_submit_wait_remove_locked(m_job_queue_t *queue,
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

/**
 * @brief   Enqueue a job and wake a waiting worker.
 */
static void _m_job_enqueue_job_locked(m_job_queue_t *queue,
                                      m_job_handle_t *job)
{
    queue->ring[queue->tail] = job;
    queue->tail = (queue->tail + 1) % queue->capacity;
    queue->count++;
    queue->stats.submitted++;
    _m_job_wake_worker_locked(queue);
}

/**
 * @brief   Wait for free space in the queue, optionally with a deadline.
 */
static m_job_error_t _m_job_wait_for_space(m_job_queue_t *queue,
                                            const m_timer_deadline_t *deadline)
{
    m_job_submit_wait_node_t *node = NULL;
    while (queue->count >= queue->capacity) {
        if (queue->destroyed) {
            _m_job_queue_unlock(queue);
            return M_JOB_ERR_DESTROYED;
        }

        if (queue->shutdown_requested) {
            _m_job_queue_unlock(queue);
            return M_JOB_ERR_SHUTDOWN;
        }

        if (node == NULL) {
            node = pvPortMalloc(sizeof(*node));
            if (node == NULL) {
                _m_job_queue_unlock(queue);
                return M_JOB_ERR_NO_MEMORY;
            }
            memset(node, 0, sizeof(*node));
            m_sched_wait_context_prepare_with_reason(&node->ctx,
                                                     M_SCHED_WAIT_REASON_JOB);
        }

        node->linked = true;
        node->next = NULL;
        if (queue->submit_waiters_tail) {
            queue->submit_waiters_tail->next = node;
        } else {
            queue->submit_waiters_head = node;
        }
        queue->submit_waiters_tail = node;

        _m_job_queue_unlock(queue);
        m_sched_wait_result_t wait_res = m_sched_wait_block(&node->ctx, deadline);
        _m_job_queue_lock(queue);

        if (node->linked) {
            _m_job_submit_wait_remove_locked(queue, node);
        }

        if (wait_res != M_SCHED_WAIT_RESULT_OK) {
            vPortFree(node);
            queue->stats.dropped++;
            m_job_error_t err = (wait_res == M_SCHED_WAIT_RESULT_TIMEOUT)
                                ? M_JOB_ERR_TIMEOUT
                                : (wait_res == M_SCHED_WAIT_RESULT_OBJECT_DESTROYED)
                                        ? M_JOB_ERR_DESTROYED
                                        : M_JOB_ERR_SHUTDOWN;
            return err;
        }

        vPortFree(node);
        node = NULL;
    }

    return M_JOB_OK;
}

m_job_error_t _m_job_queue_take(m_job_queue_t *queue,
                                 m_job_handle_t **out,
                                 m_job_worker_t *worker)
{
    *out = NULL;
    _m_job_queue_lock(queue);
    while (queue->count == 0) {
        if (queue->destroyed) {
            _m_job_queue_unlock(queue);
            return M_JOB_ERR_DESTROYED;
        }

        if (queue->shutdown_requested) {
            _m_job_queue_unlock(queue);
            return M_JOB_ERR_SHUTDOWN;
        }

        m_sched_wait_context_prepare_with_reason(&worker->wait,
                                                 M_SCHED_WAIT_REASON_JOB);
        _m_job_worker_wait_append_locked(queue, worker);
        _m_job_queue_unlock(queue);

        m_sched_wait_result_t wait_res = m_sched_wait_block(&worker->wait, NULL);

        _m_job_queue_lock(queue);
        _m_job_worker_wait_remove_locked(queue, worker);
        if (wait_res != M_SCHED_WAIT_RESULT_OK) {
            _m_job_queue_unlock(queue);
            return (wait_res == M_SCHED_WAIT_RESULT_OBJECT_DESTROYED)
                           ? M_JOB_ERR_DESTROYED
                           : M_JOB_ERR_SHUTDOWN;
        }
    }

    m_job_handle_t *job = queue->ring[queue->head];
    queue->head = (queue->head + 1) % queue->capacity;
    queue->count--;
    _m_job_wake_submitter_locked(queue);
    _m_job_queue_unlock(queue);

    *out = job;
    return M_JOB_OK;
}

m_job_queue_t *m_job_queue_create(const m_job_queue_config_t *config)
{
    if (config == NULL || config->capacity == 0 || config->worker_count == 0
        || config->name == NULL) {
        return NULL;
    }

    if (config->capacity > CONFIG_MAGNOLIA_JOB_QUEUE_CAPACITY_MAX
        || config->worker_count > CONFIG_MAGNOLIA_JOB_QUEUE_WORKER_COUNT_MAX) {
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
    queue->worker_priority = config->priority;
    queue->debug = config->debug_log;

    queue->ring = pvPortMalloc(sizeof(m_job_handle_t *) * queue->capacity);
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

    queue->lock = xSemaphoreCreateMutexStatic(&queue->lock_storage);
    if (queue->lock == NULL) {
        vPortFree(queue->workers);
        vPortFree(queue->ring);
        vPortFree(queue);
        return NULL;
    }

    queue->stats = (m_job_stats_t){0};
    queue->destroyed = false;
    queue->shutdown_requested = false;

    m_job_worker_register_scheduler_hooks();

    for (size_t i = 0; i < queue->worker_count; ++i) {
        m_job_worker_t *worker = &queue->workers[i];
        memset(worker, 0, sizeof(*worker));
        worker->queue = queue;
        worker->task_id = M_SCHED_TASK_ID_INVALID;
        worker->waiting = false;
        worker->next_waiter = NULL;
        worker->prev_waiter = NULL;
        m_sched_task_options_t opts = {
            .entry = m_job_worker_entry,
            .name = queue->name,
            .argument = worker,
            .stack_depth = config->stack_depth,
            .priority = config->priority,
            .tag = "job_worker",
            .creation_flags = M_SCHED_TASK_FLAG_WORKER,
            .user_data = queue,
        };
        if (m_sched_task_create(&opts, &worker->task_id) != M_SCHED_OK) {
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

    _m_job_queue_lock(queue);
    queue->destroyed = true;
    queue->shutdown_requested = true;
    size_t pending = queue->count;
    size_t pending_head = queue->head;
    _m_job_wake_all_submitters_locked(queue,
                                      M_SCHED_WAIT_RESULT_OBJECT_DESTROYED);
    _m_job_wake_all_workers_locked(queue,
                                   M_SCHED_WAIT_RESULT_OBJECT_DESTROYED);
    _m_job_queue_unlock(queue);

    for (size_t i = 0, idx = pending_head; i < pending; ++i) {
        m_job_handle_t *job = queue->ring[idx];
        if (job != NULL) {
            portENTER_CRITICAL(&job->lock);
            if (!job->result_ready) {
                _m_job_handle_record_cancellation(job);
            }
            portEXIT_CRITICAL(&job->lock);
        }
        idx = (idx + 1) % queue->capacity;
    }

    for (size_t i = 0; i < queue->worker_count; ++i) {
        if (queue->workers[i].task_id != M_SCHED_TASK_ID_INVALID) {
            m_sched_task_destroy(queue->workers[i].task_id);
        }
    }

    vPortFree(queue->workers);
    vPortFree(queue->ring);
    vPortFree(queue);
    return M_JOB_OK;
}

/**
 * @brief   Allocate a job handle and propagate the queue priority hint.
 */
static m_job_handle_t *m_job_create_handle(m_job_queue_t *queue,
                                            m_job_handler_t handler,
                                            void *data)
{
    m_job_id_t parent_job = jctx_current_job_id();
    m_job_handle_t *handle = _m_job_handle_create(handler, data, parent_job);
    if (handle == NULL) {
        return NULL;
    }
    if (handle->ctx != NULL) {
        uint32_t priority = queue->worker_priority;
        (void)jctx_set_field_kernel(handle->ctx,
                                    JOB_CTX_FIELD_PRIORITY_HINT,
                                    &priority,
                                    sizeof(priority));
    }
    return handle;
}

m_job_error_t m_job_queue_submit_with_handle(m_job_queue_t *queue,
                                             m_job_handler_t handler,
                                             void *data,
                                             m_job_handle_t **out_handle)
{
    if (queue == NULL || handler == NULL) {
        return M_JOB_ERR_INVALID_PARAM;
    }

    m_job_handle_t *handle = m_job_create_handle(queue, handler, data);
    if (handle == NULL) {
        return M_JOB_ERR_NO_MEMORY;
    }

    _m_job_queue_lock(queue);
    m_job_error_t err = _m_job_wait_for_space(queue, NULL);
    if (err != M_JOB_OK) {
        _m_job_queue_unlock(queue);
        vPortFree(handle);
        return err;
    }

    _m_job_enqueue_job_locked(queue, handle);
    _m_job_queue_unlock(queue);

    if (out_handle != NULL) {
        *out_handle = handle;
    }
    return M_JOB_OK;
}

m_job_error_t m_job_queue_submit_nowait_with_handle(m_job_queue_t *queue,
                                                    m_job_handler_t handler,
                                                    void *data,
                                                    m_job_handle_t **out_handle)
{
    if (queue == NULL || handler == NULL) {
        return M_JOB_ERR_INVALID_PARAM;
    }

    m_job_handle_t *handle = m_job_create_handle(queue, handler, data);
    if (handle == NULL) {
        return M_JOB_ERR_NO_MEMORY;
    }

    _m_job_queue_lock(queue);
    if (queue->count >= queue->capacity) {
        queue->stats.dropped++;
        _m_job_queue_unlock(queue);
        vPortFree(handle);
        return M_JOB_ERR_QUEUE_FULL;
    }

    _m_job_enqueue_job_locked(queue, handle);
    _m_job_queue_unlock(queue);

    if (out_handle != NULL) {
        *out_handle = handle;
    }
    return M_JOB_OK;
}

m_job_error_t m_job_queue_submit_until_with_handle(m_job_queue_t *queue,
                                                   m_job_handler_t handler,
                                                   void *data,
                                                   const m_timer_deadline_t *deadline,
                                                   m_job_handle_t **out_handle)
{
    if (queue == NULL || handler == NULL) {
        return M_JOB_ERR_INVALID_PARAM;
    }

    m_job_handle_t *handle = m_job_create_handle(queue, handler, data);
    if (handle == NULL) {
        return M_JOB_ERR_NO_MEMORY;
    }

    _m_job_queue_lock(queue);
    m_job_error_t err = _m_job_wait_for_space(queue, deadline);
    if (err != M_JOB_OK) {
        _m_job_queue_unlock(queue);
        vPortFree(handle);
        return err;
    }

    _m_job_enqueue_job_locked(queue, handle);
    _m_job_queue_unlock(queue);

    if (out_handle != NULL) {
        *out_handle = handle;
    }
    return M_JOB_OK;
}

void m_job_queue_get_info(const m_job_queue_t *queue, m_job_queue_info_t *info)
{
    if (queue == NULL || info == NULL) {
        return;
    }

    _m_job_queue_lock((m_job_queue_t *)queue);
    info->capacity = queue->capacity;
    info->depth = queue->count;
    info->worker_count = queue->worker_count;
    info->active_workers = queue->active_workers;
    info->shutdown = queue->shutdown_requested;
    info->destroyed = queue->destroyed;
    _m_job_queue_unlock((m_job_queue_t *)queue);
}

void m_job_queue_get_stats(const m_job_queue_t *queue, m_job_stats_t *stats)
{
    if (queue == NULL || stats == NULL) {
        return;
    }

    _m_job_queue_lock((m_job_queue_t *)queue);
    *stats = queue->stats;
    _m_job_queue_unlock((m_job_queue_t *)queue);
}

#ifdef CONFIG_MAGNOLIA_JOB_SELFTESTS
m_sched_task_id_t m_job_queue_get_worker_task_id(const m_job_queue_t *queue,
                                                 size_t index)
{
    if (queue == NULL || index >= queue->worker_count) {
        return M_SCHED_TASK_ID_INVALID;
    }
    return queue->workers[index].task_id;
}
#endif
