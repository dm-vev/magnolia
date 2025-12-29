/**
 * @file        kernel/core/net/m_net_errno.h
 * @brief       lwIP error mapping helpers for Magnolia networking.
 */
#ifndef MAGNOLIA_NET_M_NET_ERRNO_H
#define MAGNOLIA_NET_M_NET_ERRNO_H

#include <stdbool.h>

#include "sdkconfig.h"

#if CONFIG_MAGNOLIA_NET_ENABLE && CONFIG_MAGNOLIA_NET_BACKEND_LWIP
int m_net_errno_from_lwip_err(int lwip_err);
int m_net_errno_from_lwip_errno(int lwip_errno);
bool m_net_errno_is_timeout(int err);
#endif

#endif /* MAGNOLIA_NET_M_NET_ERRNO_H */
