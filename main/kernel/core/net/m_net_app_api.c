#include "kernel/core/elf/m_elf_app_api.h"

#include <errno.h>
#include <string.h>

#include "sdkconfig.h"
#if CONFIG_MAGNOLIA_NET_ENABLE
#include "kernel/core/net/m_net.h"
#include "kernel/core/net/m_net_lwip.h"
#endif

#if CONFIG_MAGNOLIA_NET_ENABLE
#ifndef CONFIG_MAGNOLIA_NET_MAX_DEVS
#define CONFIG_MAGNOLIA_NET_MAX_DEVS 2
#endif

static int m_net_errno_from_error(m_net_error_t err)
{
    switch (err) {
    case M_NET_OK:
        return 0;
    case M_NET_ERR_INVALID_PARAM:
        return EINVAL;
    case M_NET_ERR_BUSY:
        return EBUSY;
    case M_NET_ERR_NOT_FOUND:
        return ENODEV;
    case M_NET_ERR_TOO_MANY:
        return ENOSPC;
    case M_NET_ERR_NO_MEMORY:
        return ENOMEM;
    case M_NET_ERR_UNSUPPORTED:
        return ENOTSUP;
    case M_NET_ERR_STATE:
    default:
        return EIO;
    }
}

static m_netdev_t *m_net_app_lookup(const char *name)
{
    if (name == NULL || name[0] == '\0') {
        return m_net_get_default();
    }
    return m_net_get_by_name(name);
}

int m_net_list_ifaces(magnolia_net_iface_name_t *out,
                      size_t capacity,
                      size_t *out_count)
{
    if (out == NULL && out_count == NULL) {
        return -EINVAL;
    }

    m_netdev_t *devs[CONFIG_MAGNOLIA_NET_MAX_DEVS];
    size_t total = m_net_list(devs, CONFIG_MAGNOLIA_NET_MAX_DEVS);
    size_t limit = (capacity < total) ? capacity : total;

    if (out != NULL && capacity > 0) {
        for (size_t i = 0; i < limit; ++i) {
            const char *name = devs[i]->name ? devs[i]->name : "";
            strncpy(out[i].name, name, sizeof(out[i].name));
            out[i].name[sizeof(out[i].name) - 1] = '\0';
        }
    }

    if (out_count != NULL) {
        *out_count = total;
    }

    if (out != NULL && capacity < total) {
        return -ENOSPC;
    }
    return 0;
}

int m_net_iface_get_info(const char *name, magnolia_net_iface_info_t *out)
{
    if (out == NULL) {
        return -EINVAL;
    }

    m_netdev_t *dev = m_net_app_lookup(name);
    if (dev == NULL) {
        return -ENODEV;
    }

    magnolia_net_iface_info_t info = {0};
    info.size = (uint32_t)sizeof(info);
    info.version = 1;

    const char *dev_name = dev->name ? dev->name : "";
    strncpy(info.name, dev_name, sizeof(info.name));
    info.name[sizeof(info.name) - 1] = '\0';

    portENTER_CRITICAL(&dev->lock);
    info.state = (uint8_t)dev->state;
    info.link_state = (uint8_t)dev->link_state;
    info.mtu = dev->mtu;
    memcpy(info.mac, dev->mac, sizeof(info.mac));
    info.dhcp_enabled = dev->dhcp_enabled ? 1u : 0u;
    info.has_ipv4 = dev->has_ipv4 ? 1u : 0u;
    portEXIT_CRITICAL(&dev->lock);

    if (info.has_ipv4) {
        m_net_ipv4_t ipv4 = {0};
        if (m_net_dev_get_ipv4(dev, &ipv4) == M_NET_OK) {
            info.ipv4.addr = ipv4.addr;
            info.ipv4.mask = ipv4.mask;
            info.ipv4.gw = ipv4.gw;
        } else {
            info.has_ipv4 = 0;
        }
    }

    uint32_t want = out->size;
    if (want == 0) {
        want = (uint32_t)sizeof(info);
    }
    uint32_t copy = want;
    if (copy > (uint32_t)sizeof(info)) {
        copy = (uint32_t)sizeof(info);
    }
    memcpy(out, &info, copy);
    return 0;
}

int m_net_iface_up(const char *name)
{
    m_netdev_t *dev = m_net_app_lookup(name);
    if (dev == NULL) {
        return -ENODEV;
    }
    return -m_net_errno_from_error(m_net_dev_up(dev));
}

int m_net_iface_down(const char *name)
{
    m_netdev_t *dev = m_net_app_lookup(name);
    if (dev == NULL) {
        return -ENODEV;
    }
    return -m_net_errno_from_error(m_net_dev_down(dev));
}

int m_net_iface_set_ipv4(const char *name, const magnolia_net_ipv4_t *addr)
{
    if (addr == NULL) {
        return -EINVAL;
    }
    m_netdev_t *dev = m_net_app_lookup(name);
    if (dev == NULL) {
        return -ENODEV;
    }
    m_net_ipv4_t ipv4 = {
        .addr = addr->addr,
        .mask = addr->mask,
        .gw = addr->gw,
    };
    return -m_net_errno_from_error(m_net_dev_set_ipv4(dev, &ipv4));
}

int m_net_iface_dhcp_start(const char *name)
{
    m_netdev_t *dev = m_net_app_lookup(name);
    if (dev == NULL) {
        return -ENODEV;
    }
    return -m_net_errno_from_error(m_net_dev_start_dhcp(dev));
}

int m_net_iface_dhcp_stop(const char *name)
{
    m_netdev_t *dev = m_net_app_lookup(name);
    if (dev == NULL) {
        return -ENODEV;
    }
    return -m_net_errno_from_error(m_net_dev_stop_dhcp(dev));
}

int m_net_get_stats(const char *name, magnolia_netdev_stats_t *out)
{
    if (out == NULL) {
        return -EINVAL;
    }
    m_netdev_t *dev = m_net_app_lookup(name);
    if (dev == NULL) {
        return -ENODEV;
    }
    m_netdev_stats_t stats = {0};
    m_net_error_t err = m_net_dev_stats_snapshot(dev, &stats);
    if (err != M_NET_OK) {
        return -m_net_errno_from_error(err);
    }
    *out = (magnolia_netdev_stats_t){
        .rx_packets = stats.rx_packets,
        .rx_bytes = stats.rx_bytes,
        .rx_drops = stats.rx_drops,
        .rx_errors = stats.rx_errors,
        .tx_packets = stats.tx_packets,
        .tx_bytes = stats.tx_bytes,
        .tx_drops = stats.tx_drops,
        .tx_errors = stats.tx_errors,
    };
    return 0;
}

int m_net_get_default_iface(char *out_name, size_t name_size)
{
    if (out_name == NULL || name_size == 0) {
        return -EINVAL;
    }
    m_netdev_t *dev = m_net_get_default();
    if (dev == NULL || dev->name == NULL) {
        return -ENODEV;
    }
    strncpy(out_name, dev->name, name_size);
    out_name[name_size - 1] = '\0';
    return 0;
}

int m_net_set_default_iface(const char *name)
{
    if (name == NULL || name[0] == '\0') {
        return -EINVAL;
    }
    m_netdev_t *dev = m_net_get_by_name(name);
    if (dev == NULL) {
        return -ENODEV;
    }
    return -m_net_errno_from_error(m_net_set_default(dev));
}

int m_net_stats_snapshot_api(magnolia_net_stats_t *out)
{
    if (out == NULL) {
        return -EINVAL;
    }
    m_net_stats_t stats = {0};
    m_net_stats_snapshot(&stats);
    *out = (magnolia_net_stats_t){
        .sockets_open = stats.sockets_open,
        .rx_bytes = stats.rx_bytes,
        .tx_bytes = stats.tx_bytes,
        .rx_errors = stats.rx_errors,
        .tx_errors = stats.tx_errors,
        .rx_drops = stats.rx_drops,
        .tx_drops = stats.tx_drops,
    };
    return 0;
}

int m_net_socket_summary(magnolia_net_socket_summary_t *out,
                         size_t capacity,
                         size_t *out_count)
{
    if (out == NULL && out_count == NULL) {
        return -EINVAL;
    }
#if CONFIG_MAGNOLIA_NET_ENABLE && CONFIG_MAGNOLIA_NET_BACKEND_LWIP
    m_net_socket_summary_t summaries[CONFIG_LWIP_MAX_SOCKETS];
    size_t count = 0;
    int rc = m_net_lwip_socket_summary(summaries, CONFIG_LWIP_MAX_SOCKETS, &count);
    if (rc < 0) {
        return rc;
    }
    if (out != NULL && capacity > 0) {
        size_t copy = (capacity < count) ? capacity : count;
        for (size_t i = 0; i < copy; ++i) {
            out[i].job_id = summaries[i].job_id;
            out[i].count = summaries[i].count;
            out[i].reserved = 0;
        }
    }
    if (out_count != NULL) {
        *out_count = count;
    }
    if (out != NULL && capacity < count) {
        return -ENOSPC;
    }
    return 0;
#else
    (void)out;
    (void)capacity;
    if (out_count != NULL) {
        *out_count = 0;
    }
    return -ENOTSUP;
#endif
}
#else
int m_net_list_ifaces(magnolia_net_iface_name_t *out,
                      size_t capacity,
                      size_t *out_count)
{
    if (out == NULL && out_count == NULL) {
        return -EINVAL;
    }
    if (out != NULL && capacity > 0) {
        memset(out, 0, sizeof(*out));
    }
    if (out_count != NULL) {
        *out_count = 0;
    }
    return -ENOTSUP;
}

int m_net_iface_get_info(const char *name, magnolia_net_iface_info_t *out)
{
    (void)name;
    if (out == NULL) {
        return -EINVAL;
    }
    memset(out, 0, sizeof(*out));
    out->size = (uint32_t)sizeof(*out);
    out->version = 1;
    return -ENOTSUP;
}

int m_net_iface_up(const char *name)
{
    (void)name;
    return -ENOTSUP;
}

int m_net_iface_down(const char *name)
{
    (void)name;
    return -ENOTSUP;
}

int m_net_iface_set_ipv4(const char *name, const magnolia_net_ipv4_t *addr)
{
    (void)name;
    if (addr == NULL) {
        return -EINVAL;
    }
    return -ENOTSUP;
}

int m_net_iface_dhcp_start(const char *name)
{
    (void)name;
    return -ENOTSUP;
}

int m_net_iface_dhcp_stop(const char *name)
{
    (void)name;
    return -ENOTSUP;
}

int m_net_get_stats(const char *name, magnolia_netdev_stats_t *out)
{
    (void)name;
    if (out == NULL) {
        return -EINVAL;
    }
    memset(out, 0, sizeof(*out));
    return -ENOTSUP;
}

int m_net_get_default_iface(char *out_name, size_t name_size)
{
    if (out_name == NULL || name_size == 0) {
        return -EINVAL;
    }
    out_name[0] = '\0';
    return -ENOTSUP;
}

int m_net_set_default_iface(const char *name)
{
    if (name == NULL || name[0] == '\0') {
        return -EINVAL;
    }
    return -ENOTSUP;
}

int m_net_stats_snapshot_api(magnolia_net_stats_t *out)
{
    if (out == NULL) {
        return -EINVAL;
    }
    memset(out, 0, sizeof(*out));
    return -ENOTSUP;
}

int m_net_socket_summary(magnolia_net_socket_summary_t *out,
                         size_t capacity,
                         size_t *out_count)
{
    if (out == NULL && out_count == NULL) {
        return -EINVAL;
    }
    if (out != NULL && capacity > 0) {
        memset(out, 0, sizeof(*out));
    }
    if (out_count != NULL) {
        *out_count = 0;
    }
    return -ENOTSUP;
}
#endif
