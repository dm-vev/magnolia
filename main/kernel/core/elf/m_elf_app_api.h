#ifndef MAGNOLIA_ELF_APP_API_H
#define MAGNOLIA_ELF_APP_API_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Memory/heap statistics available to ELF applets.
 *
 * ABI notes:
 * - Caller sets `size` to sizeof(magnolia_meminfo_t) it expects.
 * - Kernel fills up to min(size, sizeof(magnolia_meminfo_t)).
 * - `version` is currently 2.
 */
typedef struct {
    uint32_t size;
    uint32_t version;

    size_t heap_total_bytes;
    size_t heap_free_bytes;
    size_t heap_min_free_bytes;
    size_t heap_largest_free_block_bytes;

    size_t job_capacity_bytes;
    size_t job_used_bytes;
    size_t job_peak_bytes;
    size_t job_region_count;
    size_t job_limit_bytes;
} magnolia_meminfo_t;

/**
 * @brief Populate memory statistics for the current system/job.
 *
 * Exported to ELF applets as `m_meminfo`.
 *
 * @return 0 on success, negative errno-style value on failure.
 */
int m_meminfo(magnolia_meminfo_t *info);

/**
 * @brief Network interface name buffer size.
 */
#define MAGNOLIA_NET_IFACE_NAME_MAX 16

typedef enum {
    MAGNOLIA_NETDEV_STATE_DOWN = 0,
    MAGNOLIA_NETDEV_STATE_UP,
} magnolia_netdev_state_t;

typedef enum {
    MAGNOLIA_NETDEV_LINK_DOWN = 0,
    MAGNOLIA_NETDEV_LINK_UP,
} magnolia_netdev_link_state_t;

typedef struct {
    char name[MAGNOLIA_NET_IFACE_NAME_MAX];
} magnolia_net_iface_name_t;

typedef struct {
    uint32_t addr;
    uint32_t mask;
    uint32_t gw;
} magnolia_net_ipv4_t;

typedef struct {
    uint64_t rx_packets;
    uint64_t rx_bytes;
    uint64_t rx_drops;
    uint64_t rx_errors;
    uint64_t tx_packets;
    uint64_t tx_bytes;
    uint64_t tx_drops;
    uint64_t tx_errors;
} magnolia_netdev_stats_t;

typedef struct {
    size_t sockets_open;
    uint64_t rx_bytes;
    uint64_t tx_bytes;
    uint64_t rx_errors;
    uint64_t tx_errors;
    uint64_t rx_drops;
    uint64_t tx_drops;
} magnolia_net_stats_t;

typedef struct {
    uint32_t size;
    uint32_t version;
    char name[MAGNOLIA_NET_IFACE_NAME_MAX];
    uint8_t state;
    uint8_t link_state;
    uint16_t mtu;
    uint8_t mac[6];
    uint8_t dhcp_enabled;
    uint8_t has_ipv4;
    uint8_t reserved[2];
    magnolia_net_ipv4_t ipv4;
} magnolia_net_iface_info_t;

typedef struct {
    uintptr_t job_id;
    uint32_t count;
    uint32_t reserved;
} magnolia_net_socket_summary_t;

int m_net_list_ifaces(magnolia_net_iface_name_t *out,
                      size_t capacity,
                      size_t *out_count);
int m_net_iface_get_info(const char *name, magnolia_net_iface_info_t *out);
int m_net_iface_up(const char *name);
int m_net_iface_down(const char *name);
int m_net_iface_set_ipv4(const char *name, const magnolia_net_ipv4_t *addr);
int m_net_iface_dhcp_start(const char *name);
int m_net_iface_dhcp_stop(const char *name);
int m_net_get_stats(const char *name, magnolia_netdev_stats_t *out);
int m_net_get_default_iface(char *out_name, size_t name_size);
int m_net_set_default_iface(const char *name);
int m_net_stats_snapshot_api(magnolia_net_stats_t *out);
int m_net_socket_summary(magnolia_net_socket_summary_t *out,
                         size_t capacity,
                         size_t *out_count);

typedef int (*magnolia_sysctl_list_cb)(const char *key,
                                       const char *value,
                                       void *ctx);

int m_sysctl_get(const char *key, char *out_value, size_t value_size);
int m_sysctl_set(const char *key, const char *value);
int m_sysctl_list(const char *prefix, magnolia_sysctl_list_cb cb, void *ctx);

#ifdef __cplusplus
}
#endif

#endif /* MAGNOLIA_ELF_APP_API_H */
