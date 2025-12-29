#include "kernel/core/net/m_net.h"

#include <stdatomic.h>
#include <string.h>

#include "sdkconfig.h"
#include "esp_log.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "kernel/core/job/m_job.h"
#include "kernel/core/net/m_net_lwip.h"

static const char *TAG = "NET";

#define NET_LOGI(fmt, ...) ESP_LOGI(TAG, "[NET] " fmt, ##__VA_ARGS__)
#define NET_LOGW(fmt, ...) ESP_LOGW(TAG, "[NET] " fmt, ##__VA_ARGS__)
#define NET_LOGE(fmt, ...) ESP_LOGE(TAG, "[NET] " fmt, ##__VA_ARGS__)

#ifndef CONFIG_MAGNOLIA_NET_MAX_DEVS
#define CONFIG_MAGNOLIA_NET_MAX_DEVS 2
#endif

#ifndef CONFIG_MAGNOLIA_NETD_STACK_DEPTH
#define CONFIG_MAGNOLIA_NETD_STACK_DEPTH 4096
#endif

static portMUX_TYPE g_net_lock = (portMUX_TYPE)portMUX_INITIALIZER_UNLOCKED;
static m_netdev_t *g_netdevs[CONFIG_MAGNOLIA_NET_MAX_DEVS];
static m_netdev_t *g_default_dev;
static m_job_queue_t *s_netd_queue;
static m_job_handle_t *s_netd_job;
static atomic_bool s_netd_shutdown = ATOMIC_VAR_INIT(false);
static atomic_int s_netd_state = ATOMIC_VAR_INIT(M_NETD_STATE_INIT);
static const char *s_netd_iface;
static atomic_size_t s_sockets_open = ATOMIC_VAR_INIT(0);

static void m_net_registry_init(void)
{
    portENTER_CRITICAL(&g_net_lock);
    for (size_t i = 0; i < CONFIG_MAGNOLIA_NET_MAX_DEVS; ++i) {
        g_netdevs[i] = NULL;
    }
    g_default_dev = NULL;
    portEXIT_CRITICAL(&g_net_lock);
}

static void m_net_apply_log_level(void)
{
    esp_log_level_set(TAG, (esp_log_level_t)CONFIG_MAGNOLIA_NET_LOG_LEVEL);
}

static const char *m_net_state_name(m_netd_state_t state)
{
    switch (state) {
    case M_NETD_STATE_INIT:
        return "INIT";
    case M_NETD_STATE_UP:
        return "UP";
    case M_NETD_STATE_DOWN:
        return "DOWN";
    case M_NETD_STATE_ERROR:
        return "ERROR";
    case M_NETD_STATE_SHUTDOWN:
        return "SHUTDOWN";
    default:
        return "UNKNOWN";
    }
}

void m_net_netd_state_set(m_netd_state_t state, const char *iface)
{
    m_netd_state_t prev = (m_netd_state_t)atomic_exchange_explicit(&s_netd_state,
                                                                   (int)state,
                                                                   memory_order_acq_rel);
    if (iface != NULL) {
        s_netd_iface = iface;
    }
    if (prev != state) {
        NET_LOGI("netd state=%s iface=%s", m_net_state_name(state), s_netd_iface ? s_netd_iface : "-");
    }
}

m_netd_state_t m_net_netd_state_get(void)
{
    return (m_netd_state_t)atomic_load_explicit(&s_netd_state, memory_order_acquire);
}

m_net_error_t m_net_register(m_netdev_t *dev)
{
    if (dev == NULL || dev->name == NULL || dev->ops == NULL) {
        return M_NET_ERR_INVALID_PARAM;
    }

    portENTER_CRITICAL(&g_net_lock);
    for (size_t i = 0; i < CONFIG_MAGNOLIA_NET_MAX_DEVS; ++i) {
        if (g_netdevs[i] == dev) {
            portEXIT_CRITICAL(&g_net_lock);
            return M_NET_ERR_BUSY;
        }
        if (g_netdevs[i] != NULL && strcmp(g_netdevs[i]->name, dev->name) == 0) {
            portEXIT_CRITICAL(&g_net_lock);
            return M_NET_ERR_BUSY;
        }
    }

    for (size_t i = 0; i < CONFIG_MAGNOLIA_NET_MAX_DEVS; ++i) {
        if (g_netdevs[i] == NULL) {
            g_netdevs[i] = dev;
            if (g_default_dev == NULL) {
                g_default_dev = dev;
            }
            dev->lock = (portMUX_TYPE)portMUX_INITIALIZER_UNLOCKED;
            portEXIT_CRITICAL(&g_net_lock);
            return M_NET_OK;
        }
    }
    portEXIT_CRITICAL(&g_net_lock);
    return M_NET_ERR_TOO_MANY;
}

m_net_error_t m_net_unregister(m_netdev_t *dev)
{
    if (dev == NULL) {
        return M_NET_ERR_INVALID_PARAM;
    }

    portENTER_CRITICAL(&g_net_lock);
    for (size_t i = 0; i < CONFIG_MAGNOLIA_NET_MAX_DEVS; ++i) {
        if (g_netdevs[i] == dev) {
            g_netdevs[i] = NULL;
            if (g_default_dev == dev) {
                g_default_dev = NULL;
            }
            portEXIT_CRITICAL(&g_net_lock);
            return M_NET_OK;
        }
    }
    portEXIT_CRITICAL(&g_net_lock);
    return M_NET_ERR_NOT_FOUND;
}

m_netdev_t *m_net_get_by_name(const char *name)
{
    if (name == NULL) {
        return NULL;
    }

    portENTER_CRITICAL(&g_net_lock);
    for (size_t i = 0; i < CONFIG_MAGNOLIA_NET_MAX_DEVS; ++i) {
        if (g_netdevs[i] != NULL && strcmp(g_netdevs[i]->name, name) == 0) {
            m_netdev_t *dev = g_netdevs[i];
            portEXIT_CRITICAL(&g_net_lock);
            return dev;
        }
    }
    portEXIT_CRITICAL(&g_net_lock);
    return NULL;
}

m_netdev_t *m_net_get_default(void)
{
    portENTER_CRITICAL(&g_net_lock);
    m_netdev_t *dev = g_default_dev;
    portEXIT_CRITICAL(&g_net_lock);
    return dev;
}

m_net_error_t m_net_set_default(m_netdev_t *dev)
{
    if (dev == NULL) {
        return M_NET_ERR_INVALID_PARAM;
    }

    portENTER_CRITICAL(&g_net_lock);
    bool found = false;
    for (size_t i = 0; i < CONFIG_MAGNOLIA_NET_MAX_DEVS; ++i) {
        if (g_netdevs[i] == dev) {
            found = true;
            break;
        }
    }
    if (found) {
        g_default_dev = dev;
        portEXIT_CRITICAL(&g_net_lock);
        return M_NET_OK;
    }
    portEXIT_CRITICAL(&g_net_lock);
    return M_NET_ERR_NOT_FOUND;
}

size_t m_net_list(m_netdev_t **out, size_t capacity)
{
    size_t count = 0;

    portENTER_CRITICAL(&g_net_lock);
    for (size_t i = 0; i < CONFIG_MAGNOLIA_NET_MAX_DEVS; ++i) {
        if (g_netdevs[i] == NULL) {
            continue;
        }
        if (out != NULL && count < capacity) {
            out[count] = g_netdevs[i];
        }
        count++;
    }
    portEXIT_CRITICAL(&g_net_lock);
    return count;
}

m_net_error_t m_net_dev_up(m_netdev_t *dev)
{
    if (dev == NULL || dev->ops == NULL || dev->ops->up == NULL) {
        return M_NET_ERR_UNSUPPORTED;
    }
    return dev->ops->up(dev) == 0 ? M_NET_OK : M_NET_ERR_STATE;
}

m_net_error_t m_net_dev_down(m_netdev_t *dev)
{
    if (dev == NULL || dev->ops == NULL || dev->ops->down == NULL) {
        return M_NET_ERR_UNSUPPORTED;
    }
    return dev->ops->down(dev) == 0 ? M_NET_OK : M_NET_ERR_STATE;
}

m_net_error_t m_net_dev_set_ipv4(m_netdev_t *dev, const m_net_ipv4_t *addr)
{
    if (dev == NULL || dev->ops == NULL || dev->ops->set_ipv4 == NULL) {
        return M_NET_ERR_UNSUPPORTED;
    }
    return dev->ops->set_ipv4(dev, addr) == 0 ? M_NET_OK : M_NET_ERR_STATE;
}

m_net_error_t m_net_dev_get_ipv4(m_netdev_t *dev, m_net_ipv4_t *out)
{
    if (dev == NULL || dev->ops == NULL || dev->ops->get_ipv4 == NULL) {
        return M_NET_ERR_UNSUPPORTED;
    }
    return dev->ops->get_ipv4(dev, out) == 0 ? M_NET_OK : M_NET_ERR_STATE;
}

m_net_error_t m_net_dev_start_dhcp(m_netdev_t *dev)
{
    if (dev == NULL || dev->ops == NULL || dev->ops->start_dhcp == NULL) {
        return M_NET_ERR_UNSUPPORTED;
    }
    return dev->ops->start_dhcp(dev) == 0 ? M_NET_OK : M_NET_ERR_STATE;
}

m_net_error_t m_net_dev_stop_dhcp(m_netdev_t *dev)
{
    if (dev == NULL || dev->ops == NULL || dev->ops->stop_dhcp == NULL) {
        return M_NET_ERR_UNSUPPORTED;
    }
    return dev->ops->stop_dhcp(dev) == 0 ? M_NET_OK : M_NET_ERR_STATE;
}

m_net_error_t m_net_dev_stats_snapshot(m_netdev_t *dev, m_netdev_stats_t *out)
{
    if (dev == NULL || out == NULL) {
        return M_NET_ERR_INVALID_PARAM;
    }

    portENTER_CRITICAL(&dev->lock);
    *out = dev->stats;
    portEXIT_CRITICAL(&dev->lock);
    return M_NET_OK;
}

void m_net_stats_record_tx(size_t bytes)
{
    if (bytes == 0) {
        return;
    }

    portENTER_CRITICAL(&g_net_lock);
    if (g_default_dev != NULL) {
        portENTER_CRITICAL(&g_default_dev->lock);
        g_default_dev->stats.tx_packets += 1;
        g_default_dev->stats.tx_bytes += bytes;
        portEXIT_CRITICAL(&g_default_dev->lock);
    }
    portEXIT_CRITICAL(&g_net_lock);
}

void m_net_stats_record_rx(size_t bytes)
{
    if (bytes == 0) {
        return;
    }

    portENTER_CRITICAL(&g_net_lock);
    if (g_default_dev != NULL) {
        portENTER_CRITICAL(&g_default_dev->lock);
        g_default_dev->stats.rx_packets += 1;
        g_default_dev->stats.rx_bytes += bytes;
        portEXIT_CRITICAL(&g_default_dev->lock);
    }
    portEXIT_CRITICAL(&g_net_lock);
}

void m_net_stats_record_error(bool tx_error)
{
    portENTER_CRITICAL(&g_net_lock);
    if (g_default_dev != NULL) {
        portENTER_CRITICAL(&g_default_dev->lock);
        if (tx_error) {
            g_default_dev->stats.tx_errors += 1;
        } else {
            g_default_dev->stats.rx_errors += 1;
        }
        portEXIT_CRITICAL(&g_default_dev->lock);
    }
    portEXIT_CRITICAL(&g_net_lock);
}

void m_net_stats_socket_open(void)
{
    atomic_fetch_add_explicit(&s_sockets_open, 1, memory_order_relaxed);
}

void m_net_stats_socket_close(void)
{
    size_t current = atomic_load_explicit(&s_sockets_open, memory_order_relaxed);
    if (current == 0) {
        return;
    }
    (void)atomic_fetch_sub_explicit(&s_sockets_open, 1, memory_order_relaxed);
}

void m_net_stats_snapshot(m_net_stats_t *out)
{
    if (out == NULL) {
        return;
    }

    memset(out, 0, sizeof(*out));
    out->sockets_open = atomic_load_explicit(&s_sockets_open, memory_order_relaxed);

    portENTER_CRITICAL(&g_net_lock);
    if (g_default_dev != NULL) {
        portENTER_CRITICAL(&g_default_dev->lock);
        out->rx_bytes = g_default_dev->stats.rx_bytes;
        out->tx_bytes = g_default_dev->stats.tx_bytes;
        out->rx_errors = g_default_dev->stats.rx_errors;
        out->tx_errors = g_default_dev->stats.tx_errors;
        out->rx_drops = g_default_dev->stats.rx_drops;
        out->tx_drops = g_default_dev->stats.tx_drops;
        portEXIT_CRITICAL(&g_default_dev->lock);
    }
    portEXIT_CRITICAL(&g_net_lock);
}

static m_job_handler_result_t m_netd_job(m_job_id_t job, void *data)
{
    (void)data;

    m_net_apply_log_level();
    m_net_netd_state_set(M_NETD_STATE_INIT, NULL);

#if CONFIG_MAGNOLIA_NET_BACKEND_LWIP
    int err = m_net_lwip_start();
    if (err != 0) {
        NET_LOGE("netd init failed err=%d", err);
        m_net_netd_state_set(M_NETD_STATE_ERROR, NULL);
    }
#else
    NET_LOGE("no network backend configured");
    m_net_netd_state_set(M_NETD_STATE_ERROR, NULL);
#endif

    while (job != NULL && !job->cancelled &&
           !atomic_load_explicit(&s_netd_shutdown, memory_order_acquire)) {
#if CONFIG_MAGNOLIA_NET_BACKEND_LWIP
        if (m_net_netd_state_get() == M_NETD_STATE_ERROR) {
            vTaskDelay(pdMS_TO_TICKS(1000));
            (void)m_net_lwip_stop();
            (void)m_net_lwip_start();
        } else {
            vTaskDelay(pdMS_TO_TICKS(500));
        }
#else
        vTaskDelay(pdMS_TO_TICKS(1000));
#endif
    }

#if CONFIG_MAGNOLIA_NET_BACKEND_LWIP
    (void)m_net_lwip_stop();
#endif
    m_net_netd_state_set(M_NETD_STATE_SHUTDOWN, NULL);
    return (m_job_handler_result_t){
        .status = M_JOB_RESULT_CANCELLED,
        .payload = NULL,
        .payload_size = 0,
    };
}

void m_net_init(void)
{
    if (s_netd_queue != NULL) {
        return;
    }

    m_net_registry_init();
    m_net_apply_log_level();
    atomic_store_explicit(&s_netd_shutdown, false, memory_order_release);
    s_netd_job = NULL;

    m_job_queue_config_t cfg = M_JOB_QUEUE_CONFIG_DEFAULT;
    cfg.name = "netd";
    cfg.capacity = 1;
    cfg.worker_count = 1;
    cfg.stack_depth = CONFIG_MAGNOLIA_NETD_STACK_DEPTH;

    s_netd_queue = m_job_queue_create(&cfg);
    if (s_netd_queue == NULL) {
        NET_LOGE("netd queue create failed");
        return;
    }

    m_job_error_t err = m_job_queue_submit_with_handle(s_netd_queue,
                                                       m_netd_job,
                                                       NULL,
                                                       &s_netd_job);
    if (err != M_JOB_OK) {
        NET_LOGE("netd submit failed err=%d", (int)err);
    }
}

m_net_error_t m_net_shutdown(void)
{
    if (s_netd_queue == NULL) {
        return M_NET_ERR_STATE;
    }

    atomic_store_explicit(&s_netd_shutdown, true, memory_order_release);
    if (s_netd_job != NULL) {
        (void)m_job_cancel(s_netd_job);
    }
#if CONFIG_MAGNOLIA_NET_BACKEND_LWIP
    (void)m_net_lwip_stop();
#endif
    (void)m_job_queue_destroy(s_netd_queue);
    s_netd_queue = NULL;
    s_netd_job = NULL;
    m_net_netd_state_set(M_NETD_STATE_SHUTDOWN, NULL);
    return M_NET_OK;
}
