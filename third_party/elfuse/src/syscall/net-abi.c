/* Socket ABI translation helpers
 *
 * Copyright 2026 elfuse contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#include <netinet/in.h>
#include <netinet/tcp.h>
#include <stdint.h>
#include <string.h>
#include <sys/socket.h>

#include "syscall/net.h"
#include "syscall/net-abi.h"

int socket_small_int_normalize(int level, int optname, int value)
{
    if ((level == LINUX_SOL_SOCKET &&
         (optname == LINUX_SO_KEEPALIVE || optname == LINUX_SO_REUSEADDR ||
          optname == LINUX_SO_ACCEPTCONN || optname == LINUX_SO_REUSEPORT ||
          optname == LINUX_SO_BROADCAST || optname == LINUX_SO_DONTROUTE ||
          optname == LINUX_SO_OOBINLINE)) ||
        (level == LINUX_IPPROTO_TCP && optname == LINUX_TCP_NODELAY) ||
        (level == LINUX_IPPROTO_IPV6 && optname == LINUX_IPV6_V6ONLY))
        return value != 0;

    if (level == LINUX_SOL_SOCKET && optname == LINUX_SO_TYPE)
        return value & 0xF;

    return value;
}

int socket_opt_uses_small_int(int level, int optname)
{
    if (level == LINUX_SOL_SOCKET) {
        switch (optname) {
        case LINUX_SO_REUSEADDR:
        case LINUX_SO_KEEPALIVE:
        case LINUX_SO_REUSEPORT:
        case LINUX_SO_BROADCAST:
        case LINUX_SO_DONTROUTE:
        case LINUX_SO_OOBINLINE:
        case LINUX_SO_RCVLOWAT:
        case LINUX_SO_SNDLOWAT:
        case LINUX_SO_RCVBUF:
        case LINUX_SO_SNDBUF:
        case LINUX_SO_TYPE:
        case LINUX_SO_ERROR:
            return 1;
        default:
            return 0;
        }
    }

    if (level == LINUX_IPPROTO_TCP) {
        switch (optname) {
        case LINUX_TCP_NODELAY:
        case LINUX_TCP_KEEPIDLE:
        case LINUX_TCP_KEEPINTVL:
        case LINUX_TCP_KEEPCNT:
            return 1;
        default:
            return 0;
        }
    }

    return level == LINUX_IPPROTO_IPV6 && optname == LINUX_IPV6_V6ONLY;
}

int translate_small_int_sockopt(int level,
                                int optname,
                                int *mac_level,
                                int *mac_optname)
{
    if (level == LINUX_SOL_SOCKET) {
        *mac_level = SOL_SOCKET;
        switch (optname) {
        case LINUX_SO_REUSEADDR:
            *mac_optname = SO_REUSEADDR;
            return 1;
        case LINUX_SO_KEEPALIVE:
            *mac_optname = SO_KEEPALIVE;
            return 1;
        case LINUX_SO_REUSEPORT:
            *mac_optname = SO_REUSEPORT;
            return 1;
        case LINUX_SO_BROADCAST:
            *mac_optname = SO_BROADCAST;
            return 1;
        case LINUX_SO_DONTROUTE:
            *mac_optname = SO_DONTROUTE;
            return 1;
        case LINUX_SO_OOBINLINE:
            *mac_optname = SO_OOBINLINE;
            return 1;
        case LINUX_SO_RCVLOWAT:
            *mac_optname = SO_RCVLOWAT;
            return 1;
        case LINUX_SO_SNDLOWAT:
            *mac_optname = SO_SNDLOWAT;
            return 1;
        case LINUX_SO_RCVBUF:
            *mac_optname = SO_RCVBUF;
            return 1;
        case LINUX_SO_SNDBUF:
            *mac_optname = SO_SNDBUF;
            return 1;
        case LINUX_SO_TYPE:
            *mac_optname = SO_TYPE;
            return 1;
        case LINUX_SO_ERROR:
            *mac_optname = SO_ERROR;
            return 1;
        default:
            return 0;
        }
    }

    if (level == LINUX_IPPROTO_TCP) {
        *mac_level = IPPROTO_TCP;
        switch (optname) {
        case LINUX_TCP_NODELAY:
            *mac_optname = TCP_NODELAY;
            return 1;
        case LINUX_TCP_KEEPIDLE:
            *mac_optname = TCP_KEEPALIVE;
            return 1;
        case LINUX_TCP_KEEPINTVL:
            *mac_optname = 0x101;
            return 1;
        case LINUX_TCP_KEEPCNT:
            *mac_optname = 0x102;
            return 1;
        default:
            return 0;
        }
    }

    if (level == LINUX_IPPROTO_IPV6 && optname == LINUX_IPV6_V6ONLY) {
        *mac_level = IPPROTO_IPV6;
        *mac_optname = IPV6_V6ONLY;
        return 1;
    }

    return 0;
}

int translate_af_to_mac(int linux_af)
{
    switch (linux_af) {
    case LINUX_AF_UNSPEC:
        return AF_UNSPEC;
    case LINUX_AF_UNIX:
        return AF_UNIX;
    case LINUX_AF_INET:
        return AF_INET;
    case LINUX_AF_INET6:
        return AF_INET6;
    default:
        return linux_af;
    }
}

static int translate_af_to_linux(int mac_af)
{
    switch (mac_af) {
    case AF_UNSPEC:
        return LINUX_AF_UNSPEC;
    case AF_UNIX:
        return LINUX_AF_UNIX;
    case AF_INET:
        return LINUX_AF_INET;
    case AF_INET6:
        return LINUX_AF_INET6;
    default:
        return mac_af;
    }
}

int linux_to_mac_sockaddr(const void *linux_sa,
                          uint32_t linux_len,
                          struct sockaddr_storage *mac_sa)
{
    if (linux_len < 2)
        return -1;

    const uint8_t *src = linux_sa;
    uint16_t linux_family;
    memcpy(&linux_family, src, 2);

    int mac_family = translate_af_to_mac((int) linux_family);

    memset(mac_sa, 0, sizeof(*mac_sa));
    mac_sa->ss_len = (uint8_t) linux_len;
    mac_sa->ss_family = (uint8_t) mac_family;

    uint32_t data_len = linux_len - 2;
    if (data_len > sizeof(*mac_sa) - 2)
        data_len = sizeof(*mac_sa) - 2;
    memcpy((uint8_t *) mac_sa + 2, src + 2, data_len);

    return (int) linux_len;
}

int mac_to_linux_sockaddr(const struct sockaddr *mac_sa,
                          socklen_t mac_len,
                          uint8_t *linux_sa,
                          uint32_t linux_buf_len)
{
    if (mac_len < 2 || linux_buf_len < 2)
        return -1;

    int linux_family = translate_af_to_linux(mac_sa->sa_family);
    uint16_t fam16 = (uint16_t) linux_family;

    memcpy(linux_sa, &fam16, 2);

    uint32_t data_len = (uint32_t) mac_len - 2;
    if (data_len > linux_buf_len - 2)
        data_len = linux_buf_len - 2;
    memcpy(linux_sa + 2, (const uint8_t *) mac_sa + 2, data_len);

    return (int) (2 + data_len);
}

int extract_sock_type(int linux_type)
{
    return linux_type & 0xF;
}

int extract_sock_nonblock(int linux_type)
{
    return (linux_type & LINUX_SOCK_NONBLOCK) != 0;
}

int extract_sock_cloexec(int linux_type)
{
    return (linux_type & LINUX_SOCK_CLOEXEC) != 0;
}

int translate_sockopt(int linux_optname)
{
    switch (linux_optname) {
    case LINUX_SO_DEBUG:
        return SO_DEBUG;
    case LINUX_SO_REUSEADDR:
        return SO_REUSEADDR;
    case LINUX_SO_TYPE:
        return SO_TYPE;
    case LINUX_SO_ERROR:
        return SO_ERROR;
    case LINUX_SO_DONTROUTE:
        return SO_DONTROUTE;
    case LINUX_SO_BROADCAST:
        return SO_BROADCAST;
    case LINUX_SO_SNDBUF:
        return SO_SNDBUF;
    case LINUX_SO_RCVBUF:
        return SO_RCVBUF;
    case LINUX_SO_KEEPALIVE:
        return SO_KEEPALIVE;
    case LINUX_SO_OOBINLINE:
        return SO_OOBINLINE;
    case LINUX_SO_LINGER:
        return SO_LINGER;
    case LINUX_SO_RCVTIMEO:
        return SO_RCVTIMEO;
    case LINUX_SO_SNDTIMEO:
        return SO_SNDTIMEO;
    case LINUX_SO_ACCEPTCONN:
        return SO_ACCEPTCONN;
    case LINUX_SO_REUSEPORT:
        return SO_REUSEPORT;
    case LINUX_SO_RCVLOWAT:
        return SO_RCVLOWAT;
    case LINUX_SO_SNDLOWAT:
        return SO_SNDLOWAT;
    default:
        return -1;
    }
}

int translate_ip_sockopt_to_mac(int linux_optname)
{
    switch (linux_optname) {
    case LINUX_IP_TOS:
        return IP_TOS;
    case LINUX_IP_TTL:
        return IP_TTL;
    case LINUX_IP_HDRINCL:
        return IP_HDRINCL;
#ifdef IP_PKTINFO
    case LINUX_IP_PKTINFO:
        return IP_PKTINFO;
#endif
#ifdef IP_RECVTTL
    case LINUX_IP_RECVTTL:
        return IP_RECVTTL;
#endif
#ifdef IP_RECVTOS
    case LINUX_IP_RECVTOS:
        return IP_RECVTOS;
#endif
    default:
        return -1;
    }
}

int translate_ip_cmsg_to_mac(int linux_type)
{
    switch (linux_type) {
    case LINUX_IP_TOS:
        return IP_TOS;
    case LINUX_IP_TTL:
        return IP_TTL;
#ifdef IP_PKTINFO
    case LINUX_IP_PKTINFO:
        return IP_PKTINFO;
#endif
    default:
        return -1;
    }
}

int translate_ip_cmsg_to_linux(int mac_type)
{
    switch (mac_type) {
    case IP_TOS:
        return LINUX_IP_TOS;
    case IP_TTL:
        return LINUX_IP_TTL;
#ifdef IP_RECVTOS
    case IP_RECVTOS:
        return LINUX_IP_TOS;
#endif
#ifdef IP_RECVTTL
    case IP_RECVTTL:
        return LINUX_IP_TTL;
#endif
#ifdef IP_PKTINFO
    case IP_PKTINFO:
        return LINUX_IP_PKTINFO;
#endif
    default:
        return -1;
    }
}

int translate_msg_flags(int linux_flags)
{
    int mac_flags = 0;
    if (linux_flags & 0x01)
        mac_flags |= MSG_OOB;
    if (linux_flags & 0x02)
        mac_flags |= MSG_PEEK;
    if (linux_flags & 0x04)
        mac_flags |= MSG_DONTROUTE;
    if (linux_flags & 0x40)
        mac_flags |= MSG_DONTWAIT;
    if (linux_flags & 0x80)
        mac_flags |= MSG_EOR;
    if (linux_flags & 0x100)
        mac_flags |= MSG_WAITALL;

    return mac_flags;
}

int mac_to_linux_msg_flags(int mac_flags)
{
    int linux_flags = 0;
    if (mac_flags & MSG_OOB)
        linux_flags |= 0x01;
    if (mac_flags & MSG_PEEK)
        linux_flags |= 0x02;
    if (mac_flags & MSG_DONTROUTE)
        linux_flags |= 0x04;
    if (mac_flags & MSG_CTRUNC)
        linux_flags |= 0x08;
    if (mac_flags & MSG_TRUNC)
        linux_flags |= 0x20;
    if (mac_flags & MSG_DONTWAIT)
        linux_flags |= 0x40;
    if (mac_flags & MSG_EOR)
        linux_flags |= 0x80;
    if (mac_flags & MSG_WAITALL)
        linux_flags |= 0x100;
    return linux_flags;
}

int sockaddr_has_zero_port(const struct sockaddr_storage *sa)
{
    if (sa->ss_family == AF_INET)
        return ((const struct sockaddr_in *) sa)->sin_port == 0;
    if (sa->ss_family == AF_INET6)
        return ((const struct sockaddr_in6 *) sa)->sin6_port == 0;
    return 0;
}

void sockaddr_set_port(struct sockaddr_storage *sa, in_port_t port)
{
    if (sa->ss_family == AF_INET)
        ((struct sockaddr_in *) sa)->sin_port = port;
    else if (sa->ss_family == AF_INET6)
        ((struct sockaddr_in6 *) sa)->sin6_port = port;
}
