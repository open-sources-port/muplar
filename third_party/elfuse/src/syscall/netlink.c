/* AF_NETLINK emulation
 *
 * Copyright 2026 elfuse contributors
 * Copyright 2025 Moritz Angermann, zw3rk pte. ltd.
 * SPDX-License-Identifier: Apache-2.0
 *
 * Emulates Linux NETLINK_ROUTE sockets so that glibc's getifaddrs()
 * works. macOS has no AF_NETLINK; netlink emulation synthesizes responses by
 * querying the host via getifaddrs(3) and formatting into Linux netlink message
 * headers (struct nlmsghdr, struct ifinfomsg, struct ifaddrmsg, etc.).
 *
 * Supported operations:
 *   socket(AF_NETLINK, SOCK_RAW|SOCK_DGRAM, NETLINK_ROUTE) -> synthetic fd
 *   bind() -> always succeeds
 *   sendmsg(RTM_GETLINK) -> builds interface list from host getifaddrs
 *   sendmsg(RTM_GETADDR) -> builds address list from host getifaddrs
 *   recvmsg() / read() -> returns buffered response data
 *
 * Non-NETLINK_ROUTE protocols return -EAFNOSUPPORT at socket() time.
 */

#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <net/if.h>
#include <ifaddrs.h>
#include <netinet/in.h>
#include <sys/socket.h>

#include "syscall/abi.h"
#include "syscall/internal.h"
#include "syscall/net.h"

static void netlink_close(int guest_fd);

/* Linux netlink message structures.
 * These structures are defined manually to match the Linux ABI exactly,
 * since macOS has no <linux/netlink.h>.
 */

/* Netlink message header (struct nlmsghdr) */
typedef struct {
    uint32_t nlmsg_len;   /* Length of message including header */
    uint16_t nlmsg_type;  /* Message type (RTM_*, NLMSG_*) */
    uint16_t nlmsg_flags; /* Additional flags */
    uint32_t nlmsg_seq;   /* Sequence number */
    uint32_t nlmsg_pid;   /* Sending process port ID */
} nlmsghdr_t;

#define NLMSG_ALIGNTO 4
#define NLMSG_ALIGN(len) (((len) + NLMSG_ALIGNTO - 1) & ~(NLMSG_ALIGNTO - 1))
#define NLMSG_HDRLEN ((int) NLMSG_ALIGN(sizeof(nlmsghdr_t)))

/* Netlink message types */
#define NLMSG_DONE 3
#define NLMSG_ERROR 2

/* RTM_* types (from linux/rtnetlink.h) */
#define RTM_GETLINK 18
#define RTM_GETADDR 22
#define RTM_NEWLINK 16
#define RTM_NEWADDR 20

/* NLM_F_* flags. Only NLM_F_MULTI is set on synthesized replies; the
 * dump-style request flags (NLM_F_ROOT|NLM_F_MATCH) are recognized in the
 * request but never echoed back, so they have no constants here.
 */
#define NLM_F_MULTI 0x02

/* Interface info message (struct ifinfomsg) */
typedef struct {
    uint8_t ifi_family, __ifi_pad;
    uint16_t ifi_type;   /* ARPHRD_* */
    int32_t ifi_index;   /* Interface index */
    uint32_t ifi_flags;  /* IFF_* flags */
    uint32_t ifi_change; /* IFF_* change mask */
} ifinfomsg_t;

/* Interface address message (struct ifaddrmsg) */
typedef struct {
    uint8_t ifa_family;    /* Address family */
    uint8_t ifa_prefixlen; /* Prefix length */
    uint8_t ifa_flags;     /* Address flags */
    uint8_t ifa_scope;     /* Address scope */
    uint32_t ifa_index;    /* Interface index */
} ifaddrmsg_t;

/* Routing attribute (struct rtattr) */
typedef struct {
    uint16_t rta_len;  /* Length including header */
    uint16_t rta_type; /* Attribute type */
} rtattr_t;

#define RTA_ALIGNTO 4
#define RTA_ALIGN(len) (((len) + RTA_ALIGNTO - 1) & ~(RTA_ALIGNTO - 1))
#define RTA_HDRLEN ((int) RTA_ALIGN(sizeof(rtattr_t)))

/* IFLA_* attribute types */
#define IFLA_IFNAME 3
#define IFLA_MTU 4

/* IFA_* attribute types */
#define IFA_ADDRESS 1
#define IFA_LOCAL 2

/* ARPHRD values */
#define ARPHRD_ETHER 1
#define ARPHRD_LOOPBACK 772

/* sockaddr_nl (Linux netlink socket address) */
typedef struct {
    uint16_t nl_family; /* AF_NETLINK */
    uint16_t nl_pad;
    uint32_t nl_pid;    /* Port ID */
    uint32_t nl_groups; /* Multicast groups mask */
} sockaddr_nl_t;

/* Per-socket state. */

#define MAX_NETLINK_FDS 16
#define NETLINK_BUF_SIZE 8192

typedef struct {
    bool in_use;
    int guest_fd;   /* Guest fd number */
    uint8_t *buf;   /* Response buffer */
    size_t buf_len; /* Bytes written into buf */
    size_t buf_pos; /* Current read position */
    uint32_t seq;   /* Sequence number from last request */
    uint32_t pid;   /* Bound PID (from bind or auto-assigned) */
} netlink_state_t;

static netlink_state_t nl_state[MAX_NETLINK_FDS];
static pthread_mutex_t nl_lock = PTHREAD_MUTEX_INITIALIZER;

#define NL_FOR_EACH(s) \
    for (netlink_state_t *s = nl_state; s < nl_state + MAX_NETLINK_FDS; s++)

/* Helpers. */

static netlink_state_t *nl_find(int guest_fd)
{
    NL_FOR_EACH (s)
        if (s->in_use && s->guest_fd == guest_fd)
            return s;
    return NULL;
}

static netlink_state_t *nl_alloc(int guest_fd)
{
    NL_FOR_EACH (s) {
        if (s->in_use)
            continue;
        memset(s, 0, sizeof(*s));
        s->in_use = true;
        s->guest_fd = guest_fd;
        s->buf = malloc(NETLINK_BUF_SIZE);
        if (!s->buf) {
            s->in_use = false;
            return NULL;
        }
        s->pid = (uint32_t) getpid();
        return s;
    }
    return NULL;
}

/* Append a netlink attribute to the buffer. Returns bytes written. */
static size_t nl_put_attr(uint8_t *buf,
                          size_t max,
                          uint16_t type,
                          const void *data,
                          uint16_t datalen)
{
    uint16_t total = (uint16_t) (RTA_HDRLEN + datalen);
    uint16_t aligned = (uint16_t) RTA_ALIGN(total);
    if (aligned > max)
        return 0;
    rtattr_t rta = {.rta_len = total, .rta_type = type};
    memcpy(buf, &rta, sizeof(rta));
    memcpy(buf + RTA_HDRLEN, data, datalen);
    /* Zero padding */
    if (aligned > total)
        memset(buf + total, 0, aligned - total);
    return aligned;
}

/* Build RTM_GETLINK response from host getifaddrs(). */
static int nl_build_getlink(netlink_state_t *ns)
{
    struct ifaddrs *ifalist, *ifa;
    if (getifaddrs(&ifalist) < 0)
        return -1;

    uint8_t *buf = ns->buf;
    size_t off = 0, max = NETLINK_BUF_SIZE;

    /* Track which interfaces netlink emulation has already emitted (by index).
     * getifaddrs returns one entry per address, but RTM_GETLINK wants one
     * message per interface.
     */
    uint32_t seen[64];
    int nseen = 0;

    for (ifa = ifalist; ifa; ifa = ifa->ifa_next) {
        unsigned int idx = if_nametoindex(ifa->ifa_name);
        if (idx == 0)
            continue;

        /* Check if already seen */
        bool found = false;
        for (int i = 0; i < nseen; i++) {
            if (seen[i] == idx) {
                found = true;
                break;
            }
        }
        if (found)
            continue;
        if (nseen < 64)
            seen[nseen++] = idx;

        /* Build message: nlmsghdr + ifinfomsg + attributes.
         * Check minimum space before advancing off.
         */
        size_t min_msg = NLMSG_HDRLEN + NLMSG_ALIGN(sizeof(ifinfomsg_t));
        if (off + min_msg > max)
            break;
        size_t msg_start = off;
        off += NLMSG_HDRLEN;

        ifinfomsg_t ifi = {0};
        ifi.ifi_family = 0; /* AF_UNSPEC */
        ifi.ifi_type =
            (!strcmp(ifa->ifa_name, "lo0")) ? ARPHRD_LOOPBACK : ARPHRD_ETHER;
        ifi.ifi_index = (int32_t) idx;
        ifi.ifi_flags = ifa->ifa_flags;
        memcpy(buf + off, &ifi, sizeof(ifi));
        off += NLMSG_ALIGN(sizeof(ifi));

        /* IFLA_IFNAME */
        size_t namelen = strlen(ifa->ifa_name) + 1;
        size_t n = nl_put_attr(buf + off, max - off, IFLA_IFNAME, ifa->ifa_name,
                               (uint16_t) namelen);
        off += n;

        /* IFLA_MTU: use a reasonable default */
        uint32_t mtu = (ifi.ifi_type == ARPHRD_LOOPBACK) ? 65536 : 1500;
        n = nl_put_attr(buf + off, max - off, IFLA_MTU, &mtu, 4);
        off += n;

        /* Backfill nlmsghdr now that nlmsg_len is known (header is reserved
         * at msg_start; attributes were written after it).
         */
        nlmsghdr_t hdr = {
            .nlmsg_len = (uint32_t) (off - msg_start),
            .nlmsg_type = RTM_NEWLINK,
            .nlmsg_flags = NLM_F_MULTI,
            .nlmsg_seq = ns->seq,
            .nlmsg_pid = ns->pid,
        };
        memcpy(buf + msg_start, &hdr, sizeof(hdr));
    }

    freeifaddrs(ifalist);

    /* Append NLMSG_DONE */
    if (off + NLMSG_HDRLEN <= max) {
        nlmsghdr_t done = {
            .nlmsg_len = NLMSG_HDRLEN,
            .nlmsg_type = NLMSG_DONE,
            .nlmsg_flags = NLM_F_MULTI,
            .nlmsg_seq = ns->seq,
            .nlmsg_pid = ns->pid,
        };
        memcpy(buf + off, &done, sizeof(done));
        off += NLMSG_HDRLEN;
    }

    ns->buf_len = off;
    ns->buf_pos = 0;
    return 0;
}

/* Build RTM_GETADDR response from host getifaddrs(). */
static int nl_build_getaddr(netlink_state_t *ns)
{
    struct ifaddrs *ifalist, *ifa;
    if (getifaddrs(&ifalist) < 0)
        return -1;

    uint8_t *buf = ns->buf;
    size_t off = 0, max = NETLINK_BUF_SIZE;

    for (ifa = ifalist; ifa; ifa = ifa->ifa_next) {
        if (!ifa->ifa_addr)
            continue;

        int family = ifa->ifa_addr->sa_family;
        if (family != AF_INET && family != AF_INET6)
            continue;

        unsigned int idx = if_nametoindex(ifa->ifa_name);
        if (idx == 0)
            continue;

        size_t min_msg = NLMSG_HDRLEN + NLMSG_ALIGN(sizeof(ifaddrmsg_t));
        if (off + min_msg > max)
            break;
        size_t msg_start = off;
        off += NLMSG_HDRLEN;

        /* Compute prefix length from netmask */
        uint8_t prefixlen = 0;
        if (ifa->ifa_netmask) {
            if (family == AF_INET) {
                uint32_t mask = ntohl(
                    ((struct sockaddr_in *) ifa->ifa_netmask)->sin_addr.s_addr);
                while (mask & 0x80000000U) {
                    prefixlen++;
                    mask <<= 1;
                }
            } else {
                struct in6_addr *m =
                    &((struct sockaddr_in6 *) ifa->ifa_netmask)->sin6_addr;
                for (int i = 0; i < 16; i++) {
                    uint8_t byte = m->s6_addr[i];
                    while (byte & 0x80) {
                        prefixlen++;
                        byte <<= 1;
                    }
                    if (byte != 0)
                        break;
                }
            }
        }

        int linux_family = (family == AF_INET) ? LINUX_AF_INET : LINUX_AF_INET6;

        ifaddrmsg_t iam = {0};
        iam.ifa_family = (uint8_t) linux_family;
        iam.ifa_prefixlen = prefixlen;
        iam.ifa_scope = 0; /* RT_SCOPE_UNIVERSE */
        iam.ifa_index = (uint32_t) idx;
        memcpy(buf + off, &iam, sizeof(iam));
        off += NLMSG_ALIGN(sizeof(iam));

        /* IFA_ADDRESS attribute */
        if (family == AF_INET) {
            struct in_addr *addr =
                &((struct sockaddr_in *) ifa->ifa_addr)->sin_addr;
            off += nl_put_attr(buf + off, max - off, IFA_ADDRESS, addr, 4);
            /* IFA_LOCAL (same for point-to-point) */
            off += nl_put_attr(buf + off, max - off, IFA_LOCAL, addr, 4);
        } else {
            struct in6_addr *addr =
                &((struct sockaddr_in6 *) ifa->ifa_addr)->sin6_addr;
            off += nl_put_attr(buf + off, max - off, IFA_ADDRESS, addr, 16);
        }

        nlmsghdr_t hdr = {
            .nlmsg_len = (uint32_t) (off - msg_start),
            .nlmsg_type = RTM_NEWADDR,
            .nlmsg_flags = NLM_F_MULTI,
            .nlmsg_seq = ns->seq,
            .nlmsg_pid = ns->pid,
        };
        memcpy(buf + msg_start, &hdr, sizeof(hdr));
    }

    freeifaddrs(ifalist);

    /* NLMSG_DONE */
    if (off + NLMSG_HDRLEN <= max) {
        nlmsghdr_t done = {
            .nlmsg_len = NLMSG_HDRLEN,
            .nlmsg_type = NLMSG_DONE,
            .nlmsg_flags = NLM_F_MULTI,
            .nlmsg_seq = ns->seq,
            .nlmsg_pid = ns->pid,
        };
        memcpy(buf + off, &done, sizeof(done));
        off += NLMSG_HDRLEN;
    }

    ns->buf_len = off;
    ns->buf_pos = 0;
    return 0;
}

/* Public API. */

void netlink_init(void)
{
    memset(nl_state, 0, sizeof(nl_state));
}

int64_t netlink_socket(int protocol, int type)
{
    (void) type;

    /* Only NETLINK_ROUTE is supported */
    if (protocol != NETLINK_ROUTE)
        return -LINUX_EAFNOSUPPORT;

    /* Allocate a pipe fd pair: the read end serves as the "socket" that
     * poll/epoll can wait on. Netlink emulation writes to the write end when
     * response data is buffered.
     */
    int pipefd[2];
    if (pipe(pipefd) < 0)
        return -LINUX_EMFILE;

    int gfd = fd_alloc(FD_NETLINK, pipefd[0], netlink_close);
    if (gfd < 0) {
        close(pipefd[0]);
        close(pipefd[1]);
        return -LINUX_EMFILE;
    }

    netlink_state_t *ns = nl_alloc(gfd);
    if (!ns) {
        fd_mark_closed(gfd);
        close(pipefd[0]);
        close(pipefd[1]);
        return -LINUX_ENOMEM;
    }

    /* No poll wakeup fd is needed because recvmsg drains the buffered response
     * directly.
     */
    close(pipefd[1]); /* The write end is unused */

    return gfd;
}

int64_t netlink_bind(int guest_fd,
                     guest_t *g,
                     uint64_t addr_gva,
                     uint32_t addrlen)
{
    netlink_state_t *ns = nl_find(guest_fd);
    if (!ns)
        return -LINUX_EBADF;

    /* Parse sockaddr_nl if provided to get nl_pid */
    if (addr_gva && addrlen >= sizeof(sockaddr_nl_t)) {
        sockaddr_nl_t snl;
        if (guest_read_small(g, addr_gva, &snl, sizeof(snl)) == 0) {
            if (snl.nl_pid != 0)
                ns->pid = snl.nl_pid;
        }
    }

    return 0;
}

int64_t netlink_sendmsg(int guest_fd, guest_t *g, uint64_t msg_gva, int flags)
{
    (void) flags;
    pthread_mutex_lock(&nl_lock);
    netlink_state_t *ns = nl_find(guest_fd);
    if (!ns) {
        pthread_mutex_unlock(&nl_lock);
        return -LINUX_EBADF;
    }

    int64_t result;

    /* Parse the linux_msghdr_t to get the iovec */
    linux_msghdr_t mhdr;
    if (guest_read_small(g, msg_gva, &mhdr, sizeof(mhdr)) < 0) {
        result = -LINUX_EFAULT;
        goto out;
    }

    if (mhdr.msg_iovlen == 0) {
        result = -LINUX_EINVAL;
        goto out;
    }

    struct {
        uint64_t iov_base, iov_len;
    } iov;
    if (guest_read_small(g, mhdr.msg_iov, &iov, sizeof(iov)) < 0) {
        result = -LINUX_EFAULT;
        goto out;
    }

    if (iov.iov_len < NLMSG_HDRLEN) {
        result = -LINUX_EINVAL;
        goto out;
    }

    nlmsghdr_t req_hdr;
    if (guest_read_small(g, iov.iov_base, &req_hdr, sizeof(req_hdr)) < 0) {
        result = -LINUX_EFAULT;
        goto out;
    }

    ns->seq = req_hdr.nlmsg_seq;

    /* Dispatch based on request type */
    int ret;
    switch (req_hdr.nlmsg_type) {
    case RTM_GETLINK:
        ret = nl_build_getlink(ns);
        break;
    case RTM_GETADDR:
        ret = nl_build_getaddr(ns);
        break;
    default:
        /* Unsupported request: return NLMSG_ERROR with EOPNOTSUPP */
        if (ns->buf_len + NLMSG_HDRLEN + 4 <= NETLINK_BUF_SIZE) {
            size_t off = 0;
            nlmsghdr_t err_hdr = {
                .nlmsg_len = NLMSG_HDRLEN + 4,
                .nlmsg_type = NLMSG_ERROR,
                .nlmsg_seq = ns->seq,
                .nlmsg_pid = ns->pid,
            };
            memcpy(ns->buf + off, &err_hdr, sizeof(err_hdr));
            off += NLMSG_HDRLEN;
            int32_t errcode = -95; /* -EOPNOTSUPP */
            memcpy(ns->buf + off, &errcode, 4);
            ns->buf_len = off + 4;
            ns->buf_pos = 0;
        }
        result = (int64_t) iov.iov_len;
        goto out;
    }

    result = (ret < 0) ? -LINUX_EIO : (int64_t) iov.iov_len;

out:
    pthread_mutex_unlock(&nl_lock);
    return result;
}

int64_t netlink_recvmsg(int guest_fd, guest_t *g, uint64_t msg_gva, int flags)
{
    (void) flags;
    pthread_mutex_lock(&nl_lock);
    netlink_state_t *ns = nl_find(guest_fd);
    if (!ns) {
        pthread_mutex_unlock(&nl_lock);
        return -LINUX_EBADF;
    }

    if (ns->buf_pos >= ns->buf_len) {
        pthread_mutex_unlock(&nl_lock);
        return 0;
    }

    /* Parse msghdr to get iovec */
    linux_msghdr_t mhdr;
    if (guest_read_small(g, msg_gva, &mhdr, sizeof(mhdr)) < 0) {
        pthread_mutex_unlock(&nl_lock);
        return -LINUX_EFAULT;
    }

    if (mhdr.msg_iovlen == 0) {
        pthread_mutex_unlock(&nl_lock);
        return 0;
    }

    struct {
        uint64_t iov_base, iov_len;
    } iov;
    if (guest_read_small(g, mhdr.msg_iov, &iov, sizeof(iov)) < 0) {
        pthread_mutex_unlock(&nl_lock);
        return -LINUX_EFAULT;
    }

    size_t avail = ns->buf_len - ns->buf_pos;
    size_t to_copy = (avail < iov.iov_len) ? avail : iov.iov_len;

    /* Return complete netlink messages only. Walk from buf_pos to find
     * the last complete message that fits in the buffer.
     */
    size_t msg_end = 0, pos = ns->buf_pos;
    while (pos < ns->buf_len && (pos - ns->buf_pos + NLMSG_HDRLEN) <= to_copy) {
        nlmsghdr_t *hdr = (nlmsghdr_t *) (ns->buf + pos);
        if (hdr->nlmsg_len < NLMSG_HDRLEN)
            break;
        size_t msg_bytes = pos - ns->buf_pos + NLMSG_ALIGN(hdr->nlmsg_len);
        if (msg_bytes > to_copy)
            break;
        pos += NLMSG_ALIGN(hdr->nlmsg_len);
        msg_end = pos - ns->buf_pos;
    }

    if (msg_end == 0) {
        /* Buffer too small for even one message. Return what fits
         * with MSG_TRUNC semantics
         */
        msg_end = to_copy;
    }

    if (guest_write(g, iov.iov_base, ns->buf + ns->buf_pos, msg_end) < 0) {
        pthread_mutex_unlock(&nl_lock);
        return -LINUX_EFAULT;
    }

    ns->buf_pos += msg_end;

    /* Write back sockaddr_nl if caller provided msg_name */
    if (mhdr.msg_name && mhdr.msg_namelen >= sizeof(sockaddr_nl_t)) {
        sockaddr_nl_t snl = {
            .nl_family = LINUX_AF_NETLINK,
            .nl_pid = 0, /* From kernel */
        };
        guest_write_small(g, mhdr.msg_name, &snl, sizeof(snl));
        uint32_t namelen = sizeof(sockaddr_nl_t);
        /* msg_namelen is at offset 4 in the msghdr (after msg_name pointer) */
        guest_write_small(g, msg_gva + 8, &namelen, sizeof(namelen));
    }

    /* Clear msg_flags and msg_controllen */
    int32_t zero_flags = 0;
    guest_write_small(g, msg_gva + offsetof(linux_msghdr_t, msg_flags),
                      &zero_flags, sizeof(zero_flags));
    uint64_t zero_controllen = 0;
    guest_write_small(g, msg_gva + offsetof(linux_msghdr_t, msg_controllen),
                      &zero_controllen, sizeof(zero_controllen));

    pthread_mutex_unlock(&nl_lock);
    return (int64_t) msg_end;
}

int64_t netlink_read(int guest_fd, guest_t *g, uint64_t buf_gva, uint64_t count)
{
    pthread_mutex_lock(&nl_lock);
    netlink_state_t *ns = nl_find(guest_fd);
    if (!ns) {
        pthread_mutex_unlock(&nl_lock);
        return -LINUX_EBADF;
    }

    if (ns->buf_pos >= ns->buf_len) {
        pthread_mutex_unlock(&nl_lock);
        return 0;
    }

    size_t avail = ns->buf_len - ns->buf_pos;
    size_t to_copy = (avail < count) ? avail : count;

    if (guest_write(g, buf_gva, ns->buf + ns->buf_pos, to_copy) < 0) {
        pthread_mutex_unlock(&nl_lock);
        return -LINUX_EFAULT;
    }

    ns->buf_pos += to_copy;
    pthread_mutex_unlock(&nl_lock);
    return (int64_t) to_copy;
}

static void netlink_close(int guest_fd)
{
    pthread_mutex_lock(&nl_lock);
    netlink_state_t *ns = nl_find(guest_fd);
    if (!ns) {
        pthread_mutex_unlock(&nl_lock);
        return;
    }
    free(ns->buf);
    ns->buf = NULL;
    ns->in_use = false;
    pthread_mutex_unlock(&nl_lock);
}
