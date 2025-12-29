/**
 * @file        kernel/core/net/m_net_lwip.h
 * @brief       lwIP backend glue for Magnolia net core.
 */
#ifndef MAGNOLIA_NET_M_NET_LWIP_H
#define MAGNOLIA_NET_M_NET_LWIP_H

#include <stddef.h>
#include <stdint.h>

#include "sdkconfig.h"

#ifdef __cplusplus
extern "C" {
#endif

#if CONFIG_MAGNOLIA_NET_ENABLE && CONFIG_MAGNOLIA_NET_BACKEND_LWIP
typedef struct {
    uintptr_t job_id;
    uint32_t count;
    uint32_t reserved;
} m_net_socket_summary_t;

int m_net_lwip_start(void);
int m_net_lwip_stop(void);
bool m_net_lwip_socket_track_add(int fd);
void m_net_lwip_socket_track_remove(int fd);
bool m_net_lwip_socket_owned(int fd);
int m_net_lwip_socket_close(int fd, bool from_job_exit);
int m_net_lwip_socket_summary(m_net_socket_summary_t *out,
                              size_t capacity,
                              size_t *out_count);
#endif

#ifdef __cplusplus
}
#endif

#endif /* MAGNOLIA_NET_M_NET_LWIP_H */
