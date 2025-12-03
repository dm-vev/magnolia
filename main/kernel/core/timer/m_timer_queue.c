/**
 * @file kernel/core/timer/m_timer_queue.c
 * @brief Magnolia timer event queue implementation.
 * @details Maintains an ordered list of timeouts, supports cancellation,
 *          and dispatches callbacks from a deterministic loop.
 */

#include "kernel/core/timer/m_timer_queue.h"

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static StaticSemaphore_t g_timer_queue_lock_storage;
static SemaphoreHandle_t g_timer_queue_lock;
static m_timer_queue_entry_t *g_timer_queue_head;

struct m_timer_queue_entry {
    m_timer_deadline_t deadline;
    m_timer_queue_callback_t callback;
    void *context;
    m_timer_queue_entry_t *next;
};

/**
 * @brief Acquire the timer queue mutex.
 */
static void m_timer_queue_lock(void)
{
    if (g_timer_queue_lock == NULL) {
        g_timer_queue_lock =
                xSemaphoreCreateMutexStatic(&g_timer_queue_lock_storage);
    }

    if (g_timer_queue_lock != NULL) {
        xSemaphoreTake(g_timer_queue_lock, portMAX_DELAY);
    }
}

/**
 * @brief Release the timer queue mutex.
 */
static void m_timer_queue_unlock(void)
{
    if (g_timer_queue_lock) {
        xSemaphoreGive(g_timer_queue_lock);
    }
}

/**
 * @brief Compare two deadlines to maintain ordering.
 */
static int m_timer_deadline_compare(const m_timer_deadline_t *a,
                                    const m_timer_deadline_t *b)
{
    if (a == NULL || a->infinite) {
        return 1;
    }
    if (b == NULL || b->infinite) {
        return -1;
    }
    if (a->target < b->target) {
        return -1;
    }
    if (a->target > b->target) {
        return 1;
    }
    return 0;
}

/**
 * @brief Check whether a deadline has already passed.
 */
static bool m_timer_deadline_expired(const m_timer_deadline_t *deadline,
                                     m_timer_time_t now)
{
    if (deadline == NULL || deadline->infinite) {
        return false;
    }
    return deadline->target <= now;
}

/**
 * @brief Insert an entry into the queue while preserving sort order.
 */
static void m_timer_queue_insert_entry(m_timer_queue_entry_t *entry)
{
    if (g_timer_queue_head == NULL
        || m_timer_deadline_compare(&entry->deadline,
                                    &g_timer_queue_head->deadline) < 0) {
        entry->next = g_timer_queue_head;
        g_timer_queue_head = entry;
        return;
    }

    m_timer_queue_entry_t *current = g_timer_queue_head;
    while (current->next != NULL
           && m_timer_deadline_compare(&entry->deadline,
                                       &current->next->deadline) >= 0) {
        current = current->next;
    }

    entry->next = current->next;
    current->next = entry;
}

void m_timer_queue_init(void)
{
    g_timer_queue_head = NULL;
    m_timer_queue_lock();
    m_timer_queue_unlock();
}

m_timer_queue_entry_t *m_timer_queue_schedule(
        m_timer_deadline_t deadline,
        m_timer_queue_callback_t callback,
        void *context)
{
    m_timer_queue_entry_t *entry = pvPortMalloc(sizeof(*entry));
    if (entry == NULL) {
        return NULL;
    }

    entry->deadline = deadline;
    entry->callback = callback;
    entry->context = context;
    entry->next = NULL;

    m_timer_queue_lock();
    m_timer_queue_insert_entry(entry);
    m_timer_queue_unlock();
    return entry;
}

bool m_timer_queue_cancel(m_timer_queue_entry_t *entry)
{
    if (entry == NULL) {
        return false;
    }

    bool removed = false;
    m_timer_queue_lock();
    m_timer_queue_entry_t **link = &g_timer_queue_head;
    while (*link != NULL) {
        if (*link == entry) {
            m_timer_queue_entry_t *target = *link;
            *link = target->next;
            removed = true;
            break;
        }
        link = &(*link)->next;
    }
    m_timer_queue_unlock();

    if (removed) {
        vPortFree(entry);
    }
    return removed;
}

void m_timer_queue_process(m_timer_time_t now)
{
    while (true) {
        m_timer_queue_entry_t *ready = NULL;
        m_timer_queue_lock();
        if (g_timer_queue_head != NULL
            && m_timer_deadline_expired(&g_timer_queue_head->deadline, now)) {
            ready = g_timer_queue_head;
            g_timer_queue_head = ready->next;
            ready->next = NULL;
        }
        m_timer_queue_unlock();

        if (ready == NULL) {
            break;
        }

        if (ready->callback) {
            ready->callback(ready, ready->context);
        }

        vPortFree(ready);
    }
}

size_t m_timer_queue_length(void)
{
    size_t count = 0;
    m_timer_queue_lock();
    m_timer_queue_entry_t *current = g_timer_queue_head;
    while (current != NULL) {
        count++;
        current = current->next;
    }
    m_timer_queue_unlock();
    return count;
}

bool m_timer_queue_next_deadline(m_timer_deadline_t *out)
{
    m_timer_queue_lock();
    bool has_next = (g_timer_queue_head != NULL);
    if (has_next && out != NULL) {
        *out = g_timer_queue_head->deadline;
    }
    m_timer_queue_unlock();
    return has_next;
}
