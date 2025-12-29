#include "kernel/core/net/m_net_errno.h"

#if CONFIG_MAGNOLIA_NET_ENABLE && CONFIG_MAGNOLIA_NET_BACKEND_LWIP

#include <errno.h>

#include "lwip/err.h"
#include "lwip/errno.h"

int m_net_errno_from_lwip_err(int lwip_err)
{
    if (lwip_err >= 0) {
        return 0;
    }

    switch ((err_t)lwip_err) {
    case ERR_TIMEOUT:
        return ETIMEDOUT;
    case ERR_CONN:
        return ENOTCONN;
    case ERR_CLSD:
        return ECONNRESET;
    case ERR_MEM:
        return ENOMEM;
    case ERR_BUF:
        return ENOBUFS;
    case ERR_USE:
        return EADDRINUSE;
    case ERR_WOULDBLOCK:
        return EAGAIN;
    default:
        break;
    }

    return err_to_errno((err_t)lwip_err);
}

int m_net_errno_from_lwip_errno(int lwip_errno)
{
    if (lwip_errno == 0) {
        return EIO;
    }

    if (lwip_errno == EWOULDBLOCK || lwip_errno == EAGAIN) {
        return ETIMEDOUT;
    }

    return lwip_errno;
}

bool m_net_errno_is_timeout(int err)
{
    return err == ETIMEDOUT;
}

#endif
