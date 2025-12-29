#include "kernel/core/net/m_net_lwip.h"

#if CONFIG_MAGNOLIA_NET_ENABLE && CONFIG_MAGNOLIA_NET_BACKEND_LWIP

#include <errno.h>
#include <string.h>

#include "sdkconfig.h"
#include "esp_err.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_netif_defaults.h"
#include "lwip/ip4_addr.h"
#include "lwip/inet.h"
#include "lwip/sockets.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "kernel/core/job/jctx.h"
#include "kernel/core/memory/m_alloc.h"
#include "kernel/core/net/m_net.h"
#include "kernel/core/net/m_net_errno.h"

#if CONFIG_MAGNOLIA_NET_USE_ETH
#include "esp_eth.h"
#include "esp_eth_mac.h"
#include "esp_eth_phy.h"
#include "esp_eth_netif_glue.h"
#endif

#if CONFIG_MAGNOLIA_NET_USE_WIFI
#include "nvs_flash.h"
#include "esp_wifi.h"
#include "esp_wifi_default.h"
#endif

static const char *TAG = "NET";

#define M_NET_SOCKET_TLS_SLOT 3

#define NET_LOGI(fmt, ...) ESP_LOGI(TAG, "[NET] " fmt, ##__VA_ARGS__)
#define NET_LOGW(fmt, ...) ESP_LOGW(TAG, "[NET] " fmt, ##__VA_ARGS__)
#define NET_LOGE(fmt, ...) ESP_LOGE(TAG, "[NET] " fmt, ##__VA_ARGS__)

#if JOB_CTX_TLS_SLOT_COUNT <= M_NET_SOCKET_TLS_SLOT
#error "Increase CONFIG_MAGNOLIA_JOB_CTX_TLS_SLOT_COUNT for network sockets"
#endif

typedef struct {
    job_ctx_t *owner;
    portMUX_TYPE lock;
    size_t count;
    size_t capacity;
    int fds[];
} m_net_socket_list_t;

typedef struct {
    job_ctx_t *owner;
    bool in_use;
} m_net_socket_entry_t;

static portMUX_TYPE s_socket_lock = (portMUX_TYPE)portMUX_INITIALIZER_UNLOCKED;
static m_net_socket_entry_t s_socket_entries[CONFIG_LWIP_MAX_SOCKETS];

static int m_net_socket_index(int fd)
{
    int idx = fd - LWIP_SOCKET_OFFSET;
    if (idx < 0 || idx >= CONFIG_LWIP_MAX_SOCKETS) {
        return -1;
    }
    return idx;
}

static job_ctx_t *m_net_socket_owner_get(int fd)
{
    int idx = m_net_socket_index(fd);
    if (idx < 0) {
        return NULL;
    }
    portENTER_CRITICAL(&s_socket_lock);
    job_ctx_t *owner = s_socket_entries[idx].in_use ? s_socket_entries[idx].owner : NULL;
    portEXIT_CRITICAL(&s_socket_lock);
    return owner;
}

static bool m_net_socket_owner_set(int fd, job_ctx_t *owner)
{
    int idx = m_net_socket_index(fd);
    if (idx < 0) {
        return false;
    }
    portENTER_CRITICAL(&s_socket_lock);
    if (s_socket_entries[idx].in_use) {
        portEXIT_CRITICAL(&s_socket_lock);
        return false;
    }
    s_socket_entries[idx].in_use = true;
    s_socket_entries[idx].owner = owner;
    portEXIT_CRITICAL(&s_socket_lock);
    return true;
}

static bool m_net_socket_owner_clear(int fd)
{
    int idx = m_net_socket_index(fd);
    if (idx < 0) {
        return false;
    }
    portENTER_CRITICAL(&s_socket_lock);
    bool was_in_use = s_socket_entries[idx].in_use;
    s_socket_entries[idx].in_use = false;
    s_socket_entries[idx].owner = NULL;
    portEXIT_CRITICAL(&s_socket_lock);
    return was_in_use;
}

int m_net_lwip_socket_summary(m_net_socket_summary_t *out,
                              size_t capacity,
                              size_t *out_count)
{
    if (out == NULL && out_count == NULL) {
        return -EINVAL;
    }

    uintptr_t ids[CONFIG_LWIP_MAX_SOCKETS];
    uint32_t counts[CONFIG_LWIP_MAX_SOCKETS];
    size_t used = 0;

    portENTER_CRITICAL(&s_socket_lock);
    for (size_t i = 0; i < CONFIG_LWIP_MAX_SOCKETS; ++i) {
        if (!s_socket_entries[i].in_use) {
            continue;
        }
        uintptr_t job_id = s_socket_entries[i].owner ?
                           (uintptr_t)s_socket_entries[i].owner->job_id : 0u;

        bool found = false;
        for (size_t j = 0; j < used; ++j) {
            if (ids[j] == job_id) {
                counts[j] += 1;
                found = true;
                break;
            }
        }
        if (!found && used < CONFIG_LWIP_MAX_SOCKETS) {
            ids[used] = job_id;
            counts[used] = 1;
            used += 1;
        }
    }
    portEXIT_CRITICAL(&s_socket_lock);

    if (out != NULL && capacity > 0) {
        size_t copy = (capacity < used) ? capacity : used;
        for (size_t i = 0; i < copy; ++i) {
            out[i].job_id = ids[i];
            out[i].count = counts[i];
            out[i].reserved = 0;
        }
    }

    if (out_count != NULL) {
        *out_count = used;
    }

    if (out != NULL && capacity < used) {
        return -ENOSPC;
    }
    return 0;
}

static void m_net_socket_list_destroy(void *ptr)
{
    m_net_socket_list_t *list = (m_net_socket_list_t *)ptr;
    if (list == NULL) {
        return;
    }

    size_t count = 0;

    portENTER_CRITICAL(&list->lock);
    count = list->count;
    list->count = 0;
    portEXIT_CRITICAL(&list->lock);

    for (size_t i = 0; i < count; ++i) {
        int fd = list->fds[i];
        list->fds[i] = -1;
        if (fd >= 0) {
            (void)m_net_lwip_socket_close(fd, true);
        }
    }

    m_job_free(list->owner, list);
}

static m_net_socket_list_t *m_net_socket_list_get(job_ctx_t *ctx, bool create)
{
    if (ctx == NULL) {
        return NULL;
    }

    m_net_socket_list_t *list = (m_net_socket_list_t *)jctx_tls_get(ctx, M_NET_SOCKET_TLS_SLOT);
    if (list != NULL || !create) {
        return list;
    }

    size_t capacity = (size_t)CONFIG_LWIP_MAX_SOCKETS;
    if (capacity == 0) {
        return NULL;
    }

    size_t bytes = sizeof(*list) + capacity * sizeof(int);
    list = (m_net_socket_list_t *)m_job_alloc(ctx, bytes);
    if (list == NULL) {
        return NULL;
    }

    list->owner = ctx;
    list->count = 0;
    list->capacity = capacity;
    list->lock = (portMUX_TYPE)portMUX_INITIALIZER_UNLOCKED;
    for (size_t i = 0; i < capacity; ++i) {
        list->fds[i] = -1;
    }

    if (jctx_tls_set(ctx, M_NET_SOCKET_TLS_SLOT, list, m_net_socket_list_destroy) != JOB_CTX_OK) {
        m_job_free(ctx, list);
        return NULL;
    }

    return list;
}

bool m_net_lwip_socket_track_add(int fd)
{
    job_ctx_t *ctx = jctx_current();
    if (ctx == NULL) {
        return false;
    }

    m_net_socket_list_t *list = m_net_socket_list_get(ctx, true);
    if (list == NULL) {
        return false;
    }

    if (!m_net_socket_owner_set(fd, ctx)) {
        NET_LOGW("sock job=%p fd=%d already owned", ctx->job_id, fd);
        return false;
    }

    portENTER_CRITICAL(&list->lock);
    if (list->count >= list->capacity) {
        portEXIT_CRITICAL(&list->lock);
        NET_LOGW("sock job=%p fd=%d list full", ctx->job_id, fd);
        (void)m_net_socket_owner_clear(fd);
        return false;
    }

    list->fds[list->count++] = fd;
    portEXIT_CRITICAL(&list->lock);
    m_net_stats_socket_open();
    return true;
}

void m_net_lwip_socket_track_remove(int fd)
{
    job_ctx_t *owner = m_net_socket_owner_get(fd);
    if (owner == NULL) {
        return;
    }
    m_net_socket_list_t *list = m_net_socket_list_get(owner, false);
    if (list == NULL) {
        (void)m_net_socket_owner_clear(fd);
        return;
    }

    portENTER_CRITICAL(&list->lock);
    for (size_t i = 0; i < list->count; ++i) {
        if (list->fds[i] == fd) {
            size_t last = list->count - 1;
            list->fds[i] = list->fds[last];
            list->fds[last] = -1;
            list->count--;
            break;
        }
    }
    portEXIT_CRITICAL(&list->lock);

    if (m_net_socket_owner_clear(fd)) {
        m_net_stats_socket_close();
    }
}

bool m_net_lwip_socket_owned(int fd)
{
    job_ctx_t *owner = m_net_socket_owner_get(fd);
    job_ctx_t *ctx = jctx_current();
    if (owner == NULL || ctx == NULL) {
        return false;
    }
    return owner == ctx;
}

int m_net_lwip_socket_close(int fd, bool from_job_exit)
{
    if (xPortInIsrContext()) {
        return EPERM;
    }

    job_ctx_t *owner = m_net_socket_owner_get(fd);
    job_ctx_t *ctx = jctx_current();
    if (owner == NULL || (!from_job_exit && owner != ctx)) {
        return EBADF;
    }

    m_net_socket_list_t *list = m_net_socket_list_get(owner, false);
    if (list != NULL) {
        portENTER_CRITICAL(&list->lock);
        for (size_t i = 0; i < list->count; ++i) {
            if (list->fds[i] == fd) {
                size_t last = list->count - 1;
                list->fds[i] = list->fds[last];
                list->fds[last] = -1;
                list->count--;
                break;
            }
        }
        portEXIT_CRITICAL(&list->lock);
    }

    if (m_net_socket_owner_clear(fd)) {
        m_net_stats_socket_close();
    }

    (void)lwip_shutdown(fd, SHUT_RDWR);
    int rc = lwip_close(fd);
    int lwip_errno = (rc < 0) ? errno : 0;
    int err = (rc < 0) ? m_net_errno_from_lwip_errno(lwip_errno) : 0;
    if (rc < 0) {
        NET_LOGW("sock job=%p fd=%d op=close lwip_errno=%d errno=%d",
                 owner ? owner->job_id : NULL,
                 fd,
                 lwip_errno,
                 err);
        m_net_stats_record_error(true);
    }
    if (from_job_exit) {
        NET_LOGI("sock job=%p fd=%d auto-closed on exit", owner ? owner->job_id : NULL, fd);
    }
    return err;
}

typedef struct {
    m_netdev_t dev;
    esp_netif_t *netif;
    bool is_wifi;
    bool dhcp;
    bool has_ip;
    bool netdev_registered;
#if CONFIG_MAGNOLIA_NET_USE_ETH
    esp_eth_handle_t eth_handle;
    esp_eth_netif_glue_handle_t eth_glue;
    esp_eth_mac_t *eth_mac;
    esp_eth_phy_t *eth_phy;
#endif
#if CONFIG_MAGNOLIA_NET_USE_WIFI
    bool wifi_inited;
#endif
    esp_event_handler_instance_t if_event;
    esp_event_handler_instance_t ip_event;
} m_net_lwip_dev_t;

static m_net_lwip_dev_t s_lwip_dev;
static bool s_lwip_started;

static bool m_net_lwip_parse_ipv4(const char *value, esp_ip4_addr_t *out)
{
    if (value == NULL || value[0] == '\0') {
        return false;
    }
    ip4_addr_t addr = {0};
    if (!ip4addr_aton(value, &addr)) {
        return false;
    }
    *out = addr;
    return true;
}

static void m_net_lwip_log_mac(const char *name, const uint8_t *mac)
{
    if (mac == NULL) {
        return;
    }
    NET_LOGI("%s mac=%02x:%02x:%02x:%02x:%02x:%02x",
             name,
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

static void m_net_lwip_log_ip(const char *name, const esp_netif_ip_info_t *ip)
{
    if (ip == NULL) {
        return;
    }
    char ip_buf[16];
    char mask_buf[16];
    char gw_buf[16];

    ip4addr_ntoa_r(&ip->ip, ip_buf, sizeof(ip_buf));
    ip4addr_ntoa_r(&ip->netmask, mask_buf, sizeof(mask_buf));
    ip4addr_ntoa_r(&ip->gw, gw_buf, sizeof(gw_buf));

    NET_LOGI("%s ip=%s mask=%s gw=%s", name, ip_buf, mask_buf, gw_buf);
}

static void m_net_lwip_set_state(m_net_lwip_dev_t *ldev,
                                 m_netdev_state_t state,
                                 m_netdev_link_state_t link)
{
    if (ldev == NULL) {
        return;
    }

    portENTER_CRITICAL(&ldev->dev.lock);
    ldev->dev.state = state;
    ldev->dev.link_state = link;
    portEXIT_CRITICAL(&ldev->dev.lock);
}

static void m_net_lwip_sync_flags(m_net_lwip_dev_t *ldev)
{
    if (ldev == NULL) {
        return;
    }

    portENTER_CRITICAL(&ldev->dev.lock);
    ldev->dev.dhcp_enabled = ldev->dhcp;
    ldev->dev.has_ipv4 = ldev->has_ip;
    portEXIT_CRITICAL(&ldev->dev.lock);
}

static void m_net_lwip_cleanup_dev(m_net_lwip_dev_t *ldev)
{
    if (ldev == NULL) {
        return;
    }

    if (ldev->dev.name != NULL) {
        m_net_lwip_set_state(ldev, M_NETDEV_STATE_DOWN, M_NETDEV_LINK_DOWN);
    }
    ldev->has_ip = false;
    ldev->dhcp = false;
    m_net_lwip_sync_flags(ldev);

    if (ldev->if_event != NULL) {
#if CONFIG_MAGNOLIA_NET_USE_WIFI && CONFIG_MAGNOLIA_NET_USE_ETH
        if (ldev->is_wifi) {
            (void)esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, ldev->if_event);
        } else {
            (void)esp_event_handler_instance_unregister(ETH_EVENT, ESP_EVENT_ANY_ID, ldev->if_event);
        }
#elif CONFIG_MAGNOLIA_NET_USE_WIFI
        if (ldev->is_wifi) {
            (void)esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, ldev->if_event);
        }
#elif CONFIG_MAGNOLIA_NET_USE_ETH
        if (!ldev->is_wifi) {
            (void)esp_event_handler_instance_unregister(ETH_EVENT, ESP_EVENT_ANY_ID, ldev->if_event);
        }
#endif
        ldev->if_event = NULL;
    }

    if (ldev->ip_event != NULL) {
        (void)esp_event_handler_instance_unregister(IP_EVENT, ESP_EVENT_ANY_ID, ldev->ip_event);
        ldev->ip_event = NULL;
    }

#if CONFIG_MAGNOLIA_NET_USE_WIFI
    if (ldev->is_wifi) {
        (void)esp_wifi_disconnect();
        (void)esp_wifi_stop();
        if (ldev->wifi_inited) {
            (void)esp_wifi_deinit();
            ldev->wifi_inited = false;
        }
    }
#endif

#if CONFIG_MAGNOLIA_NET_USE_ETH
    if (!ldev->is_wifi) {
        if (ldev->eth_handle != NULL) {
            (void)esp_eth_stop(ldev->eth_handle);
        }
    }
#endif

#if CONFIG_MAGNOLIA_NET_USE_ETH
    if (ldev->eth_glue != NULL) {
        (void)esp_eth_del_netif_glue(ldev->eth_glue);
        ldev->eth_glue = NULL;
    }
    if (ldev->eth_handle != NULL) {
        (void)esp_eth_driver_uninstall(ldev->eth_handle);
        ldev->eth_handle = NULL;
    }
    if (ldev->eth_phy != NULL) {
        ldev->eth_phy->del(ldev->eth_phy);
        ldev->eth_phy = NULL;
    }
    if (ldev->eth_mac != NULL) {
        ldev->eth_mac->del(ldev->eth_mac);
        ldev->eth_mac = NULL;
    }
#endif

    if (ldev->netif != NULL) {
        esp_netif_destroy(ldev->netif);
        ldev->netif = NULL;
    }

    if (ldev->netdev_registered) {
        (void)m_net_unregister(&ldev->dev);
        ldev->netdev_registered = false;
    }
}

static void m_net_lwip_event_handler(void *arg,
                                     esp_event_base_t base,
                                     int32_t id,
                                     void *data)
{
    m_net_lwip_dev_t *ldev = (m_net_lwip_dev_t *)arg;
    if (ldev == NULL) {
        return;
    }

#if CONFIG_MAGNOLIA_NET_USE_ETH
    if (base == ETH_EVENT) {
        switch (id) {
        case ETH_EVENT_CONNECTED:
            m_net_lwip_set_state(ldev, ldev->dev.state, M_NETDEV_LINK_UP);
            ldev->has_ip = !ldev->dhcp;
            m_net_lwip_sync_flags(ldev);
            NET_LOGI("%s link up", ldev->dev.name);
            if (ldev->has_ip) {
                m_net_netd_state_set(M_NETD_STATE_UP, ldev->dev.name);
            } else {
                m_net_netd_state_set(M_NETD_STATE_DOWN, ldev->dev.name);
            }
            break;
        case ETH_EVENT_DISCONNECTED:
            m_net_lwip_set_state(ldev, ldev->dev.state, M_NETDEV_LINK_DOWN);
            ldev->has_ip = false;
            m_net_lwip_sync_flags(ldev);
            NET_LOGI("%s link down", ldev->dev.name);
            m_net_netd_state_set(M_NETD_STATE_DOWN, ldev->dev.name);
            break;
        case ETH_EVENT_START:
            m_net_lwip_set_state(ldev, M_NETDEV_STATE_UP, ldev->dev.link_state);
            NET_LOGI("%s started", ldev->dev.name);
            break;
        case ETH_EVENT_STOP:
            m_net_lwip_set_state(ldev, M_NETDEV_STATE_DOWN, M_NETDEV_LINK_DOWN);
            ldev->has_ip = false;
            m_net_lwip_sync_flags(ldev);
            NET_LOGI("%s stopped", ldev->dev.name);
            m_net_netd_state_set(M_NETD_STATE_DOWN, ldev->dev.name);
            break;
        default:
            break;
        }
        return;
    }
#endif

#if CONFIG_MAGNOLIA_NET_USE_WIFI
    if (base == WIFI_EVENT) {
        switch (id) {
        case WIFI_EVENT_STA_START:
            m_net_lwip_set_state(ldev, M_NETDEV_STATE_UP, ldev->dev.link_state);
            NET_LOGI("%s started", ldev->dev.name);
            (void)esp_wifi_connect();
            break;
        case WIFI_EVENT_STA_CONNECTED:
            m_net_lwip_set_state(ldev, ldev->dev.state, M_NETDEV_LINK_UP);
            ldev->has_ip = !ldev->dhcp;
            m_net_lwip_sync_flags(ldev);
            NET_LOGI("%s link up", ldev->dev.name);
            if (ldev->has_ip) {
                m_net_netd_state_set(M_NETD_STATE_UP, ldev->dev.name);
            } else {
                m_net_netd_state_set(M_NETD_STATE_DOWN, ldev->dev.name);
            }
            break;
        case WIFI_EVENT_STA_DISCONNECTED:
            m_net_lwip_set_state(ldev, ldev->dev.state, M_NETDEV_LINK_DOWN);
            ldev->has_ip = false;
            m_net_lwip_sync_flags(ldev);
            NET_LOGI("%s link down", ldev->dev.name);
            m_net_netd_state_set(M_NETD_STATE_DOWN, ldev->dev.name);
            (void)esp_wifi_connect();
            break;
        case WIFI_EVENT_STA_STOP:
            m_net_lwip_set_state(ldev, M_NETDEV_STATE_DOWN, M_NETDEV_LINK_DOWN);
            ldev->has_ip = false;
            m_net_lwip_sync_flags(ldev);
            NET_LOGI("%s stopped", ldev->dev.name);
            m_net_netd_state_set(M_NETD_STATE_DOWN, ldev->dev.name);
            break;
        default:
            break;
        }
        return;
    }
#endif

    if (base == IP_EVENT) {
        if (id == IP_EVENT_STA_GOT_IP || id == IP_EVENT_ETH_GOT_IP) {
            ip_event_got_ip_t *event = (ip_event_got_ip_t *)data;
            if (event != NULL) {
                ldev->has_ip = true;
                m_net_lwip_sync_flags(ldev);
                m_net_lwip_log_ip(ldev->dev.name, &event->ip_info);
                m_net_netd_state_set(M_NETD_STATE_UP, ldev->dev.name);
            }
        } else if (id == IP_EVENT_STA_LOST_IP || id == IP_EVENT_ETH_LOST_IP) {
            ldev->has_ip = false;
            m_net_lwip_sync_flags(ldev);
            NET_LOGI("%s lost ip", ldev->dev.name);
            m_net_netd_state_set(M_NETD_STATE_DOWN, ldev->dev.name);
        }
    }
}

static int m_net_lwip_apply_ip_config(m_net_lwip_dev_t *ldev)
{
    if (ldev == NULL || ldev->netif == NULL) {
        return -1;
    }

    if (CONFIG_MAGNOLIA_NET_DHCP) {
        esp_err_t err = esp_netif_dhcpc_start(ldev->netif);
        if (err != ESP_OK && err != ESP_ERR_ESP_NETIF_DHCP_ALREADY_STARTED) {
            NET_LOGE("%s dhcp start failed: %s", ldev->dev.name, esp_err_to_name(err));
            return -1;
        }
        ldev->dhcp = true;
        ldev->has_ip = false;
        m_net_lwip_sync_flags(ldev);
        return 0;
    }

    esp_ip4_addr_t ip = {0};
    esp_ip4_addr_t mask = {0};
    esp_ip4_addr_t gw = {0};

    if (!m_net_lwip_parse_ipv4(CONFIG_MAGNOLIA_NET_STATIC_IP, &ip) ||
            !m_net_lwip_parse_ipv4(CONFIG_MAGNOLIA_NET_STATIC_MASK, &mask) ||
            !m_net_lwip_parse_ipv4(CONFIG_MAGNOLIA_NET_STATIC_GW, &gw)) {
        NET_LOGE("static IP config invalid");
        return -1;
    }

    esp_err_t err = esp_netif_dhcpc_stop(ldev->netif);
    if (err != ESP_OK && err != ESP_ERR_ESP_NETIF_DHCP_ALREADY_STOPPED) {
        NET_LOGE("%s dhcp stop failed: %s", ldev->dev.name, esp_err_to_name(err));
        return -1;
    }

    esp_netif_ip_info_t info = {
        .ip = ip,
        .netmask = mask,
        .gw = gw,
    };

    err = esp_netif_set_ip_info(ldev->netif, &info);
    if (err != ESP_OK) {
        NET_LOGE("%s set ip failed: %s", ldev->dev.name, esp_err_to_name(err));
        return -1;
    }

    ldev->dhcp = false;
    ldev->has_ip = true;
    m_net_lwip_sync_flags(ldev);
    m_net_lwip_log_ip(ldev->dev.name, &info);
    return 0;
}

static int m_net_lwip_dev_up(m_netdev_t *dev)
{
    if (dev == NULL) {
        return -1;
    }

    m_net_lwip_dev_t *ldev = (m_net_lwip_dev_t *)dev->driver_data;
    if (ldev == NULL) {
        return -1;
    }

#if CONFIG_MAGNOLIA_NET_USE_WIFI
    if (ldev->is_wifi) {
        esp_err_t err = esp_wifi_start();
        if (err != ESP_OK && err != ESP_ERR_WIFI_NOT_STOPPED) {
            NET_LOGE("%s wifi start failed: %s", dev->name, esp_err_to_name(err));
            return -1;
        }
        (void)esp_wifi_connect();
        return 0;
    }
#endif

#if CONFIG_MAGNOLIA_NET_USE_ETH
    esp_err_t err = esp_eth_start(ldev->eth_handle);
    if (err != ESP_OK) {
        NET_LOGE("%s eth start failed: %s", dev->name, esp_err_to_name(err));
        return -1;
    }
    return 0;
#else
    return -1;
#endif
}

static int m_net_lwip_dev_down(m_netdev_t *dev)
{
    if (dev == NULL) {
        return -1;
    }

    m_net_lwip_dev_t *ldev = (m_net_lwip_dev_t *)dev->driver_data;
    if (ldev == NULL) {
        return -1;
    }

#if CONFIG_MAGNOLIA_NET_USE_WIFI
    if (ldev->is_wifi) {
        (void)esp_wifi_disconnect();
        esp_err_t err = esp_wifi_stop();
        if (err != ESP_OK) {
            NET_LOGE("%s wifi stop failed: %s", dev->name, esp_err_to_name(err));
            return -1;
        }
        return 0;
    }
#endif

#if CONFIG_MAGNOLIA_NET_USE_ETH
    esp_err_t err = esp_eth_stop(ldev->eth_handle);
    if (err != ESP_OK) {
        NET_LOGE("%s eth stop failed: %s", dev->name, esp_err_to_name(err));
        return -1;
    }
    return 0;
#else
    return -1;
#endif
}

static m_netdev_link_state_t m_net_lwip_dev_get_link_state(m_netdev_t *dev)
{
    if (dev == NULL) {
        return M_NETDEV_LINK_DOWN;
    }
    portENTER_CRITICAL(&dev->lock);
    m_netdev_link_state_t state = dev->link_state;
    portEXIT_CRITICAL(&dev->lock);
    return state;
}

static int m_net_lwip_dev_set_ipv4(m_netdev_t *dev, const m_net_ipv4_t *addr)
{
    if (dev == NULL || addr == NULL) {
        return -1;
    }

    m_net_lwip_dev_t *ldev = (m_net_lwip_dev_t *)dev->driver_data;
    if (ldev == NULL || ldev->netif == NULL) {
        return -1;
    }

    esp_err_t err = esp_netif_dhcpc_stop(ldev->netif);
    if (err != ESP_OK && err != ESP_ERR_ESP_NETIF_DHCP_ALREADY_STOPPED) {
        NET_LOGE("%s dhcp stop failed: %s", dev->name, esp_err_to_name(err));
        return -1;
    }

    esp_netif_ip_info_t info = {
        .ip = { .addr = addr->addr },
        .netmask = { .addr = addr->mask },
        .gw = { .addr = addr->gw },
    };

    err = esp_netif_set_ip_info(ldev->netif, &info);
    if (err != ESP_OK) {
        NET_LOGE("%s set ip failed: %s", dev->name, esp_err_to_name(err));
        return -1;
    }

    ldev->dhcp = false;
    ldev->has_ip = true;
    m_net_lwip_sync_flags(ldev);
    m_net_lwip_log_ip(dev->name, &info);
    portENTER_CRITICAL(&dev->lock);
    m_netdev_link_state_t link = dev->link_state;
    portEXIT_CRITICAL(&dev->lock);
    if (link == M_NETDEV_LINK_UP) {
        m_net_netd_state_set(M_NETD_STATE_UP, dev->name);
    }
    return 0;
}

static int m_net_lwip_dev_get_ipv4(m_netdev_t *dev, m_net_ipv4_t *out)
{
    if (dev == NULL || out == NULL) {
        return -1;
    }

    m_net_lwip_dev_t *ldev = (m_net_lwip_dev_t *)dev->driver_data;
    if (ldev == NULL || ldev->netif == NULL) {
        return -1;
    }

    esp_netif_ip_info_t info = {0};
    esp_err_t err = esp_netif_get_ip_info(ldev->netif, &info);
    if (err != ESP_OK) {
        return -1;
    }

    out->addr = info.ip.addr;
    out->mask = info.netmask.addr;
    out->gw = info.gw.addr;
    return 0;
}

static int m_net_lwip_dev_start_dhcp(m_netdev_t *dev)
{
    if (dev == NULL) {
        return -1;
    }

    m_net_lwip_dev_t *ldev = (m_net_lwip_dev_t *)dev->driver_data;
    if (ldev == NULL || ldev->netif == NULL) {
        return -1;
    }

    esp_err_t err = esp_netif_dhcpc_start(ldev->netif);
    if (err != ESP_OK && err != ESP_ERR_ESP_NETIF_DHCP_ALREADY_STARTED) {
        NET_LOGE("%s dhcp start failed: %s", dev->name, esp_err_to_name(err));
        return -1;
    }
    ldev->dhcp = true;
    ldev->has_ip = false;
    m_net_lwip_sync_flags(ldev);
    m_net_netd_state_set(M_NETD_STATE_DOWN, dev->name);
    return 0;
}

static int m_net_lwip_dev_stop_dhcp(m_netdev_t *dev)
{
    if (dev == NULL) {
        return -1;
    }

    m_net_lwip_dev_t *ldev = (m_net_lwip_dev_t *)dev->driver_data;
    if (ldev == NULL || ldev->netif == NULL) {
        return -1;
    }

    esp_err_t err = esp_netif_dhcpc_stop(ldev->netif);
    if (err != ESP_OK && err != ESP_ERR_ESP_NETIF_DHCP_ALREADY_STOPPED) {
        NET_LOGE("%s dhcp stop failed: %s", dev->name, esp_err_to_name(err));
        return -1;
    }
    ldev->dhcp = false;
    ldev->has_ip = false;
    m_net_lwip_sync_flags(ldev);
    m_net_netd_state_set(M_NETD_STATE_DOWN, dev->name);
    return 0;
}

static const m_netdev_ops_t s_lwip_ops = {
    .up = m_net_lwip_dev_up,
    .down = m_net_lwip_dev_down,
    .get_link_state = m_net_lwip_dev_get_link_state,
    .set_ipv4 = m_net_lwip_dev_set_ipv4,
    .get_ipv4 = m_net_lwip_dev_get_ipv4,
    .start_dhcp = m_net_lwip_dev_start_dhcp,
    .stop_dhcp = m_net_lwip_dev_stop_dhcp,
};

static void m_net_lwip_dev_init(m_net_lwip_dev_t *ldev, const char *name, bool is_wifi)
{
    memset(ldev, 0, sizeof(*ldev));
    ldev->dev.name = name;
    ldev->dev.state = M_NETDEV_STATE_DOWN;
    ldev->dev.link_state = M_NETDEV_LINK_DOWN;
    ldev->dev.mtu = 1500;
    ldev->dev.dhcp_enabled = false;
    ldev->dev.has_ipv4 = false;
    ldev->dev.ops = &s_lwip_ops;
    ldev->dev.driver_data = ldev;
    ldev->dev.lock = (portMUX_TYPE)portMUX_INITIALIZER_UNLOCKED;
    ldev->is_wifi = is_wifi;
}

#if CONFIG_MAGNOLIA_NET_USE_ETH
static esp_eth_phy_t *m_net_lwip_create_phy(eth_phy_config_t *phy_config)
{
#if CONFIG_ETH_PHY_LAN8720
    return esp_eth_phy_new_lan8720(phy_config);
#elif CONFIG_ETH_PHY_IP101
    return esp_eth_phy_new_ip101(phy_config);
#elif CONFIG_ETH_PHY_RTL8201
    return esp_eth_phy_new_rtl8201(phy_config);
#elif CONFIG_ETH_PHY_LAN87XX
    return esp_eth_phy_new_lan87xx(phy_config);
#elif CONFIG_ETH_PHY_DP83848
    return esp_eth_phy_new_dp83848(phy_config);
#elif CONFIG_ETH_PHY_KSZ80XX
    return esp_eth_phy_new_ksz80xx(phy_config);
#elif CONFIG_ETH_PHY_GENERIC
    return esp_eth_phy_new_generic(phy_config);
#else
    return NULL;
#endif
}

static int m_net_lwip_init_eth(m_net_lwip_dev_t *ldev)
{
#if !CONFIG_ETH_USE_ESP32_EMAC
    NET_LOGE("ESP32 EMAC disabled in IDF config");
    return -1;
#endif
#if !CONFIG_IDF_TARGET_ESP32
    NET_LOGE("ESP32 EMAC only supported on ESP32 targets");
    return -1;
#endif

    m_net_lwip_cleanup_dev(ldev);
    m_net_lwip_dev_init(ldev, "eth0", false);

    esp_netif_config_t cfg = ESP_NETIF_DEFAULT_ETH();
    ldev->netif = esp_netif_new(&cfg);
    if (ldev->netif == NULL) {
        NET_LOGE("eth netif create failed");
        goto fail;
    }

    esp_netif_set_default_netif(ldev->netif);

    eth_mac_config_t mac_config = ETH_MAC_DEFAULT_CONFIG();
    eth_phy_config_t phy_config = ETH_PHY_DEFAULT_CONFIG();
    eth_esp32_emac_config_t emac_config = ETH_ESP32_EMAC_DEFAULT_CONFIG();
    emac_config.smi_gpio.mdc_num = CONFIG_ETH_MDC_GPIO;
    emac_config.smi_gpio.mdio_num = CONFIG_ETH_MDIO_GPIO;
    esp_eth_mac_t *mac = esp_eth_mac_new_esp32(&emac_config, &mac_config);
    esp_eth_phy_t *phy = m_net_lwip_create_phy(&phy_config);
    if (mac == NULL || phy == NULL) {
        NET_LOGE("eth mac/phy init failed");
        goto fail;
    }
    ldev->eth_mac = mac;
    ldev->eth_phy = phy;

    esp_eth_config_t config = ETH_DEFAULT_CONFIG(mac, phy);
    esp_err_t err = esp_eth_driver_install(&config, &ldev->eth_handle);
    if (err != ESP_OK) {
        NET_LOGE("eth driver install failed: %s", esp_err_to_name(err));
        goto fail;
    }

    ldev->eth_glue = esp_eth_new_netif_glue(ldev->eth_handle);
    if (ldev->eth_glue == NULL) {
        NET_LOGE("eth netif glue failed");
        goto fail;
    }

    err = esp_netif_attach(ldev->netif, ldev->eth_glue);
    if (err != ESP_OK) {
        NET_LOGE("eth netif attach failed: %s", esp_err_to_name(err));
        goto fail;
    }

    err = esp_event_handler_instance_register(ETH_EVENT, ESP_EVENT_ANY_ID,
                                              m_net_lwip_event_handler, ldev,
                                              &ldev->if_event);
    if (err != ESP_OK) {
        NET_LOGE("eth event handler failed: %s", esp_err_to_name(err));
        goto fail;
    }

    err = esp_event_handler_instance_register(IP_EVENT, ESP_EVENT_ANY_ID,
                                              m_net_lwip_event_handler, ldev,
                                              &ldev->ip_event);
    if (err != ESP_OK) {
        NET_LOGE("ip event handler failed: %s", esp_err_to_name(err));
        goto fail;
    }

    if (m_net_lwip_apply_ip_config(ldev) != 0) {
        goto fail;
    }

    err = esp_eth_start(ldev->eth_handle);
    if (err != ESP_OK) {
        NET_LOGE("eth start failed: %s", esp_err_to_name(err));
        goto fail;
    }

    uint8_t mac_addr[6] = {0};
    if (esp_netif_get_mac(ldev->netif, mac_addr) == ESP_OK) {
        memcpy(ldev->dev.mac, mac_addr, sizeof(ldev->dev.mac));
        m_net_lwip_log_mac(ldev->dev.name, ldev->dev.mac);
    }

    if (m_net_register(&ldev->dev) != M_NET_OK) {
        NET_LOGE("eth netdev register failed");
        goto fail;
    }
    ldev->netdev_registered = true;

    m_net_netd_state_set(M_NETD_STATE_DOWN, ldev->dev.name);
    NET_LOGI("%s ready", ldev->dev.name);
    return 0;

fail:
    m_net_lwip_cleanup_dev(ldev);
    return -1;
}
#endif

#if CONFIG_MAGNOLIA_NET_USE_WIFI
static int m_net_lwip_init_wifi(m_net_lwip_dev_t *ldev)
{
    m_net_lwip_cleanup_dev(ldev);
    m_net_lwip_dev_init(ldev, "wifi0", true);

    if (CONFIG_MAGNOLIA_NET_WIFI_SSID[0] == '\0') {
        NET_LOGE("wifi ssid not configured");
        return -1;
    }

    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        err = nvs_flash_erase();
        if (err == ESP_OK) {
            err = nvs_flash_init();
        }
    }
    if (err != ESP_OK) {
        NET_LOGE("nvs init failed: %s", esp_err_to_name(err));
        return -1;
    }

    esp_netif_config_t cfg = ESP_NETIF_DEFAULT_WIFI_STA();
    ldev->netif = esp_netif_new(&cfg);
    if (ldev->netif == NULL) {
        NET_LOGE("wifi netif create failed");
        goto fail;
    }

    esp_netif_set_default_netif(ldev->netif);
    err = esp_netif_attach_wifi_station(ldev->netif);
    if (err != ESP_OK) {
        NET_LOGE("wifi netif attach failed: %s", esp_err_to_name(err));
        goto fail;
    }

    err = esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                              m_net_lwip_event_handler, ldev,
                                              &ldev->if_event);
    if (err != ESP_OK) {
        NET_LOGE("wifi event handler failed: %s", esp_err_to_name(err));
        goto fail;
    }

    err = esp_event_handler_instance_register(IP_EVENT, ESP_EVENT_ANY_ID,
                                              m_net_lwip_event_handler, ldev,
                                              &ldev->ip_event);
    if (err != ESP_OK) {
        NET_LOGE("ip event handler failed: %s", esp_err_to_name(err));
        goto fail;
    }

    wifi_init_config_t wifi_cfg = WIFI_INIT_CONFIG_DEFAULT();
    err = esp_wifi_init(&wifi_cfg);
    if (err != ESP_OK) {
        NET_LOGE("wifi init failed: %s", esp_err_to_name(err));
        goto fail;
    }
    ldev->wifi_inited = true;

    (void)esp_wifi_set_storage(WIFI_STORAGE_RAM);

    err = esp_wifi_set_mode(WIFI_MODE_STA);
    if (err != ESP_OK) {
        NET_LOGE("wifi mode set failed: %s", esp_err_to_name(err));
        goto fail;
    }

    wifi_config_t cfg_sta = {0};
    strlcpy((char *)cfg_sta.sta.ssid, CONFIG_MAGNOLIA_NET_WIFI_SSID, sizeof(cfg_sta.sta.ssid));
    strlcpy((char *)cfg_sta.sta.password, CONFIG_MAGNOLIA_NET_WIFI_PASSWORD, sizeof(cfg_sta.sta.password));

    err = esp_wifi_set_config(WIFI_IF_STA, &cfg_sta);
    if (err != ESP_OK) {
        NET_LOGE("wifi set config failed: %s", esp_err_to_name(err));
        goto fail;
    }

    if (m_net_lwip_apply_ip_config(ldev) != 0) {
        goto fail;
    }

    err = esp_wifi_start();
    if (err != ESP_OK) {
        NET_LOGE("wifi start failed: %s", esp_err_to_name(err));
        goto fail;
    }

    uint8_t mac_addr[6] = {0};
    if (esp_wifi_get_mac(WIFI_IF_STA, mac_addr) == ESP_OK) {
        memcpy(ldev->dev.mac, mac_addr, sizeof(ldev->dev.mac));
        m_net_lwip_log_mac(ldev->dev.name, ldev->dev.mac);
    }

    if (m_net_register(&ldev->dev) != M_NET_OK) {
        NET_LOGE("wifi netdev register failed");
        goto fail;
    }
    ldev->netdev_registered = true;

    m_net_netd_state_set(M_NETD_STATE_DOWN, ldev->dev.name);
    NET_LOGI("%s ready", ldev->dev.name);
    return 0;

fail:
    m_net_lwip_cleanup_dev(ldev);
    return -1;
}
#endif

int m_net_lwip_start(void)
{
    if (s_lwip_started) {
        return 0;
    }

    esp_err_t err = esp_netif_init();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        NET_LOGE("esp_netif_init failed: %s", esp_err_to_name(err));
        return -1;
    }

    err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        NET_LOGE("event loop init failed: %s", esp_err_to_name(err));
        return -1;
    }

#if CONFIG_MAGNOLIA_NET_USE_ETH
    if (m_net_lwip_init_eth(&s_lwip_dev) == 0) {
        s_lwip_started = true;
        return 0;
    }
    NET_LOGW("eth init failed, trying wifi");
#endif

#if CONFIG_MAGNOLIA_NET_USE_WIFI
    if (m_net_lwip_init_wifi(&s_lwip_dev) == 0) {
        s_lwip_started = true;
        return 0;
    }
#endif

    NET_LOGE("no network interface initialized");
    return -1;
}

int m_net_lwip_stop(void)
{
    m_net_lwip_cleanup_dev(&s_lwip_dev);
    s_lwip_started = false;
    return 0;
}

#endif /* CONFIG_MAGNOLIA_NET_ENABLE && CONFIG_MAGNOLIA_NET_BACKEND_LWIP */
