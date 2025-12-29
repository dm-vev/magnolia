/**
 * @file        kernel/core/net/m_net.h
 * @brief       Magnolia network core API and netdev abstraction.
 *
 * Invariants for Magnolia v1.0 networking:
 * - Single active netdev (eth0 or wifi0).
 * - IPv4-only addressing.
 * - Socket API is blocking; O_NONBLOCK/MSG_DONTWAIT return ENOTSUP.
 * - poll/select support lwIP sockets only (VFS fds via poll in libc).
 */
#ifndef MAGNOLIA_NET_M_NET_H
#define MAGNOLIA_NET_M_NET_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    M_NET_OK = 0,
    M_NET_ERR_INVALID_PARAM,
    M_NET_ERR_BUSY,
    M_NET_ERR_NOT_FOUND,
    M_NET_ERR_TOO_MANY,
    M_NET_ERR_NO_MEMORY,
    M_NET_ERR_UNSUPPORTED,
    M_NET_ERR_STATE,
} m_net_error_t;

typedef enum {
    M_NETD_STATE_INIT = 0,
    M_NETD_STATE_UP,
    M_NETD_STATE_DOWN,
    M_NETD_STATE_ERROR,
    M_NETD_STATE_SHUTDOWN,
} m_netd_state_t;

typedef enum {
    M_NETDEV_STATE_DOWN = 0,
    M_NETDEV_STATE_UP,
} m_netdev_state_t;

typedef enum {
    M_NETDEV_LINK_DOWN = 0,
    M_NETDEV_LINK_UP,
} m_netdev_link_state_t;

typedef struct {
    uint32_t addr;
    uint32_t mask;
    uint32_t gw;
} m_net_ipv4_t;

typedef struct {
    uint64_t rx_packets;
    uint64_t rx_bytes;
    uint64_t rx_drops;
    uint64_t rx_errors;
    uint64_t tx_packets;
    uint64_t tx_bytes;
    uint64_t tx_drops;
    uint64_t tx_errors;
} m_netdev_stats_t;

typedef struct {
    size_t sockets_open;
    uint64_t rx_bytes;
    uint64_t tx_bytes;
    uint64_t rx_errors;
    uint64_t tx_errors;
    uint64_t rx_drops;
    uint64_t tx_drops;
} m_net_stats_t;

struct m_netdev;

typedef struct {
    int (*up)(struct m_netdev *dev);
    int (*down)(struct m_netdev *dev);
    m_netdev_link_state_t (*get_link_state)(struct m_netdev *dev);
    int (*set_ipv4)(struct m_netdev *dev, const m_net_ipv4_t *addr);
    int (*get_ipv4)(struct m_netdev *dev, m_net_ipv4_t *out);
    int (*start_dhcp)(struct m_netdev *dev);
    int (*stop_dhcp)(struct m_netdev *dev);
} m_netdev_ops_t;

typedef struct m_netdev {
    const char *name;
    m_netdev_state_t state;
    m_netdev_link_state_t link_state;
    uint16_t mtu;
    uint8_t mac[6];
    bool dhcp_enabled;
    bool has_ipv4;
    m_netdev_stats_t stats;
    const m_netdev_ops_t *ops;
    void *driver_data;
    portMUX_TYPE lock;
} m_netdev_t;

void m_net_init(void);
m_net_error_t m_net_shutdown(void);
m_netd_state_t m_net_netd_state_get(void);
void m_net_netd_state_set(m_netd_state_t state, const char *iface);

m_net_error_t m_net_register(m_netdev_t *dev);
m_net_error_t m_net_unregister(m_netdev_t *dev);
m_netdev_t *m_net_get_by_name(const char *name);
m_netdev_t *m_net_get_default(void);
m_net_error_t m_net_set_default(m_netdev_t *dev);
size_t m_net_list(m_netdev_t **out, size_t capacity);

m_net_error_t m_net_dev_up(m_netdev_t *dev);
m_net_error_t m_net_dev_down(m_netdev_t *dev);
m_net_error_t m_net_dev_set_ipv4(m_netdev_t *dev, const m_net_ipv4_t *addr);
m_net_error_t m_net_dev_get_ipv4(m_netdev_t *dev, m_net_ipv4_t *out);
m_net_error_t m_net_dev_start_dhcp(m_netdev_t *dev);
m_net_error_t m_net_dev_stop_dhcp(m_netdev_t *dev);
m_net_error_t m_net_dev_stats_snapshot(m_netdev_t *dev, m_netdev_stats_t *out);

void m_net_stats_record_tx(size_t bytes);
void m_net_stats_record_rx(size_t bytes);
void m_net_stats_record_error(bool tx_error);
void m_net_stats_socket_open(void);
void m_net_stats_socket_close(void);
void m_net_stats_snapshot(m_net_stats_t *out);

#ifdef __cplusplus
}
#endif

#endif /* MAGNOLIA_NET_M_NET_H */
