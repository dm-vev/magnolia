#include <stddef.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdatomic.h>

#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"

#include "kernel/core/vfs/core/m_vfs_object.h"
#include "kernel/core/vfs/core/m_vfs_wait.h"

#if CONFIG_MAGNOLIA_VFS_NODE_LIFETIME_CHECK
static atomic_size_t g_vfs_node_live_count = ATOMIC_VAR_INIT(0);

static void
_m_vfs_node_lifetime_log(const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    printf("[vfs/lifetime] ");
    vprintf(fmt, args);
    printf("\n");
    va_end(args);
}

static bool
_m_vfs_node_lifetime_dump_cb(const m_vfs_node_t *node, void *user_data)
{
    (void)user_data;
    if (node == NULL) {
        return true;
    }

    size_t count = atomic_load_explicit(&node->refcount,
                                        memory_order_relaxed);
    if (count == 0) {
        return true;
    }

    _m_vfs_node_lifetime_log("leaked node %p type=%u refcount=%zu",
                             (void *)node,
                             node->type,
                             count);
    return true;
}

void
m_vfs_node_lifetime_check_report(void)
{
    size_t live = atomic_load_explicit(&g_vfs_node_live_count,
                                       memory_order_relaxed);
    _m_vfs_node_lifetime_log("active nodes: %zu", live);
    if (live > 0) {
        m_vfs_node_iterate(_m_vfs_node_lifetime_dump_cb, NULL);
    }
}
#else
void
m_vfs_node_lifetime_check_report(void)
{
}
#endif

static portMUX_TYPE g_vfs_node_list_lock =
        (portMUX_TYPE)portMUX_INITIALIZER_UNLOCKED;
static m_vfs_node_t *g_vfs_node_list;

static void _m_vfs_node_list_add(m_vfs_node_t *node)
{
    if (node == NULL) {
        return;
    }

    portENTER_CRITICAL(&g_vfs_node_list_lock);
    node->list_next = g_vfs_node_list;
    g_vfs_node_list = node;
    portEXIT_CRITICAL(&g_vfs_node_list_lock);
}

static void _m_vfs_node_list_remove(m_vfs_node_t *node)
{
    if (node == NULL) {
        return;
    }

    portENTER_CRITICAL(&g_vfs_node_list_lock);
    m_vfs_node_t **slot = &g_vfs_node_list;
    while (*slot != NULL) {
        if (*slot == node) {
            *slot = node->list_next;
            node->list_next = NULL;
            break;
        }
        slot = &(*slot)->list_next;
    }
    portEXIT_CRITICAL(&g_vfs_node_list_lock);
}

m_vfs_node_t *m_vfs_node_create(m_vfs_mount_t *mount,
                                 m_vfs_node_type_t type)
{
    if (mount == NULL) {
        return NULL;
    }

    m_vfs_node_t *node = pvPortMalloc(sizeof(*node));
    if (node == NULL) {
        return NULL;
    }

    node->fs_type = mount->fs_type;
    node->mount = mount;
    node->parent = NULL;
    node->type = type;
    node->lock = (portMUX_TYPE)portMUX_INITIALIZER_UNLOCKED;
    atomic_init(&node->refcount, 1);
    node->fs_private = NULL;
    node->destroyed = false;
    node->list_next = NULL;

#if CONFIG_MAGNOLIA_VFS_NODE_LIFETIME_CHECK
    atomic_fetch_add_explicit(&g_vfs_node_live_count,
                              1,
                              memory_order_relaxed);
#endif

    _m_vfs_node_list_add(node);
    return node;
}

void m_vfs_node_acquire(m_vfs_node_t *node)
{
    if (node == NULL) {
        return;
    }

    atomic_fetch_add_explicit(&node->refcount,
                              1,
                              memory_order_relaxed);
}

static void
_m_vfs_node_free(m_vfs_node_t *node)
{
    if (node == NULL) {
        return;
    }

    _m_vfs_node_list_remove(node);

#if CONFIG_MAGNOLIA_VFS_NODE_LIFETIME_CHECK
    atomic_fetch_sub_explicit(&g_vfs_node_live_count,
                              1,
                              memory_order_relaxed);
#endif

    if (node->fs_type != NULL &&
            node->fs_type->ops != NULL &&
            node->fs_type->ops->node_destroy != NULL) {
        node->fs_type->ops->node_destroy(node);
    } else {
        vPortFree(node);
    }
}

void m_vfs_node_release(m_vfs_node_t *node)
{
    if (node == NULL) {
        return;
    }

    size_t previous = atomic_fetch_sub_explicit(&node->refcount,
                                               1,
                                               memory_order_acq_rel);

    if (previous == 0) {
#if CONFIG_MAGNOLIA_VFS_NODE_LIFETIME_CHECK
        _m_vfs_node_lifetime_log("double release detected on node %p",
                                 (void *)node);
#endif
        atomic_fetch_add_explicit(&node->refcount,
                                  1,
                                  memory_order_relaxed);
        return;
    }

    if (previous == 1) {
        node->destroyed = true;
        _m_vfs_node_free(node);
    }
}

m_vfs_file_t *m_vfs_file_create(m_vfs_node_t *node)
{
    if (node == NULL) {
        return NULL;
    }

    m_vfs_file_t *file = pvPortMalloc(sizeof(*file));
    if (file == NULL) {
        return NULL;
    }

    m_vfs_node_acquire(node);
    file->node = node;
    file->lock = (portMUX_TYPE)portMUX_INITIALIZER_UNLOCKED;
    atomic_init(&file->refcount, 1);
    file->offset = 0;
    file->fs_private = NULL;
    file->closed = false;
    file->destroyed = false;
    file->wait_lock = (portMUX_TYPE)portMUX_INITIALIZER_UNLOCKED;
    ipc_wait_queue_init(&file->waiters);
    return file;
}

void m_vfs_file_acquire(m_vfs_file_t *file)
{
    if (file == NULL) {
        return;
    }

    atomic_fetch_add_explicit(&file->refcount,
                              1,
                              memory_order_relaxed);
}

static void
_m_vfs_file_free(m_vfs_file_t *file)
{
    if (file == NULL) {
        return;
    }

    m_vfs_node_t *node = file->node;
    file->destroyed = true;
    m_vfs_file_wake(file, IPC_WAIT_RESULT_OBJECT_DESTROYED);

    if (file->node != NULL &&
            file->node->fs_type != NULL &&
            file->node->fs_type->ops != NULL &&
            file->node->fs_type->ops->file_destroy != NULL) {
        file->node->fs_type->ops->file_destroy(file);
    } else {
        vPortFree(file);
    }

    if (node != NULL) {
        m_vfs_node_release(node);
    }
}

void m_vfs_file_release(m_vfs_file_t *file)
{
    if (file == NULL) {
        return;
    }

    size_t previous = atomic_fetch_sub_explicit(&file->refcount,
                                               1,
                                               memory_order_acq_rel);

    if (previous == 0) {
        atomic_fetch_add_explicit(&file->refcount,
                                  1,
                                  memory_order_relaxed);
        return;
    }

    if (previous == 1) {
        _m_vfs_file_free(file);
    }
}

void m_vfs_file_set_offset(m_vfs_file_t *file, size_t offset)
{
    if (file == NULL) {
        return;
    }

    portENTER_CRITICAL(&file->lock);
    file->offset = offset;
    portEXIT_CRITICAL(&file->lock);
}

ipc_wait_result_t
m_vfs_file_wait(m_vfs_file_t *file,
                m_sched_wait_reason_t reason,
                const m_timer_deadline_t *deadline)
{
    if (file == NULL) {
        return IPC_WAIT_RESULT_OBJECT_DESTROYED;
    }

    ipc_waiter_t waiter = {0};
    ipc_waiter_prepare(&waiter, reason);

    portENTER_CRITICAL(&file->wait_lock);
    if (file->destroyed || file->closed) {
        portEXIT_CRITICAL(&file->wait_lock);
        return IPC_WAIT_RESULT_OBJECT_DESTROYED;
    }
    ipc_waiter_enqueue(&file->waiters, &waiter);
    portEXIT_CRITICAL(&file->wait_lock);

    ipc_wait_result_t result = ipc_waiter_block(&waiter, deadline);

    portENTER_CRITICAL(&file->wait_lock);
    ipc_waiter_remove(&file->waiters, &waiter);
    portEXIT_CRITICAL(&file->wait_lock);

    return result;
}

void
m_vfs_file_wake(m_vfs_file_t *file, ipc_wait_result_t result)
{
    if (file == NULL) {
        return;
    }

    portENTER_CRITICAL(&file->wait_lock);
    ipc_wake_all(&file->waiters, result);
    portEXIT_CRITICAL(&file->wait_lock);
}

void
m_vfs_file_notify_event(m_vfs_file_t *file)
{
    m_vfs_file_wake(file, IPC_WAIT_RESULT_OK);
}

void
m_vfs_node_iterate(m_vfs_node_iter_fn cb, void *user_data)
{
    if (cb == NULL) {
        return;
    }

    portENTER_CRITICAL(&g_vfs_node_list_lock);
    m_vfs_node_t *iter = g_vfs_node_list;
    while (iter != NULL) {
        if (!cb(iter, user_data)) {
            break;
        }
        iter = iter->list_next;
    }
    portEXIT_CRITICAL(&g_vfs_node_list_lock);
}
