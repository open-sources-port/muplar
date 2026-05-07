/* Socket/networking syscalls
 *
 * Copyright 2026 elfuse contributors
 * Copyright 2025 Moritz Angermann, zw3rk pte. ltd.
 * SPDX-License-Identifier: Apache-2.0
 *
 * Translates Linux socket syscalls into macOS equivalents, handling the
 * differences in address family constants, sockaddr layout (sa_len byte),
 * socket option constants, and flag encoding (SOCK_NONBLOCK/SOCK_CLOEXEC
 * packed into the type argument).
 */

#pragma once

#include <stdint.h>
#include "core/guest.h"

/* Linux address families. */
#define LINUX_AF_UNSPEC 0
#define LINUX_AF_UNIX 1
#define LINUX_AF_INET 2
#define LINUX_AF_INET6 10
#define LINUX_AF_NETLINK 16

/* Linux Netlink protocol families. */
#define NETLINK_ROUTE 0

/* Linux socket types + flags. */
#define LINUX_SOCK_NONBLOCK 0x800
#define LINUX_SOCK_CLOEXEC 0x80000

/* Linux SOL_SOCKET option level. */
#define LINUX_SOL_SOCKET 1

/* Linux cmsg types for AF_UNIX. */
#define LINUX_SCM_CREDENTIALS 2

/* Linux ucred structure (12 bytes on aarch64). */
typedef struct {
    int32_t pid;
    uint32_t uid, gid;
} linux_ucred_t;

/* Linux SOL_SOCKET option names (differ from macOS!) */
#define LINUX_SO_DEBUG 1
#define LINUX_SO_REUSEADDR 2
#define LINUX_SO_TYPE 3
#define LINUX_SO_ERROR 4
#define LINUX_SO_DONTROUTE 5
#define LINUX_SO_BROADCAST 6
#define LINUX_SO_SNDBUF 7
#define LINUX_SO_RCVBUF 8
#define LINUX_SO_KEEPALIVE 9
#define LINUX_SO_OOBINLINE 10
#define LINUX_SO_LINGER 13
#define LINUX_SO_RCVTIMEO 20
#define LINUX_SO_SNDTIMEO 21
#define LINUX_SO_PASSCRED 16
#define LINUX_SO_PEERCRED 17
#define LINUX_SO_ACCEPTCONN 30
#define LINUX_SO_REUSEPORT 15
#define LINUX_SO_RCVLOWAT 18
#define LINUX_SO_SNDLOWAT 19

/* Linux TCP level options. */
#define LINUX_IPPROTO_TCP 6
#define LINUX_TCP_NODELAY 1
#define LINUX_TCP_KEEPIDLE 4 /* -> macOS TCP_KEEPALIVE */
#define LINUX_TCP_KEEPINTVL 5
#define LINUX_TCP_KEEPCNT 6

/* Linux IP/IPv6 level options. */
#define LINUX_IPPROTO_IP 0
#define LINUX_IPPROTO_IPV6 41
#define LINUX_IP_TOS 1
#define LINUX_IP_TTL 2
#define LINUX_IP_HDRINCL 3
#define LINUX_IP_PKTINFO 8
#define LINUX_IP_MTU_DISCOVER 10
#define LINUX_IP_RECVERR 11
#define LINUX_IP_RECVTTL 12
#define LINUX_IP_RECVTOS 13
#define LINUX_IPV6_V6ONLY 26

/* Linux msghdr (aarch64). */
typedef struct {
    uint64_t msg_name; /* Guest pointer to sockaddr */
    uint32_t msg_namelen, _pad0;
    uint64_t msg_iov; /* Guest pointer to iovec array */
    uint64_t msg_iovlen;
    uint64_t msg_control; /* Guest pointer to ancillary data */
    uint64_t msg_controllen;
    int32_t msg_flags, _pad1;
} linux_msghdr_t;

/* Linux mmsghdr (for sendmmsg/recvmmsg). */
typedef struct {
    linux_msghdr_t msg_hdr;
    uint32_t msg_len, _pad;
} linux_mmsghdr_t;

/* Socket syscall handlers. */

int64_t sys_socket(guest_t *g, int domain, int type, int protocol);
int64_t sys_socketpair(guest_t *g,
                       int domain,
                       int type,
                       int protocol,
                       uint64_t sv_gva);
int64_t sys_bind(guest_t *g, int fd, uint64_t addr_gva, uint32_t addrlen);
int64_t sys_listen(int fd, int backlog);
int64_t sys_accept(guest_t *g, int fd, uint64_t addr_gva, uint64_t addrlen_gva);
int64_t sys_accept4(guest_t *g,
                    int fd,
                    uint64_t addr_gva,
                    uint64_t addrlen_gva,
                    int flags);
int64_t sys_connect(guest_t *g, int fd, uint64_t addr_gva, uint32_t addrlen);
int64_t sys_getsockname(guest_t *g,
                        int fd,
                        uint64_t addr_gva,
                        uint64_t addrlen_gva);
int64_t sys_getpeername(guest_t *g,
                        int fd,
                        uint64_t addr_gva,
                        uint64_t addrlen_gva);
int64_t sys_sendto(guest_t *g,
                   int fd,
                   uint64_t buf_gva,
                   uint64_t len,
                   int flags,
                   uint64_t dest_gva,
                   uint32_t addrlen);
int64_t sys_recvfrom(guest_t *g,
                     int fd,
                     uint64_t buf_gva,
                     uint64_t len,
                     int flags,
                     uint64_t src_gva,
                     uint64_t addrlen_gva);
int64_t sys_setsockopt(guest_t *g,
                       int fd,
                       int level,
                       int optname,
                       uint64_t optval_gva,
                       uint32_t optlen);
int64_t sys_getsockopt(guest_t *g,
                       int fd,
                       int level,
                       int optname,
                       uint64_t optval_gva,
                       uint64_t optlen_gva);
int64_t sys_shutdown(int fd, int how);
int64_t sys_sendmsg(guest_t *g, int fd, uint64_t msg_gva, int flags);
int64_t sys_recvmsg(guest_t *g, int fd, uint64_t msg_gva, int flags);
int64_t sys_sendmmsg(guest_t *g,
                     int fd,
                     uint64_t mmsg_gva,
                     unsigned int vlen,
                     int flags);
int64_t sys_recvmmsg(guest_t *g,
                     int fd,
                     uint64_t mmsg_gva,
                     unsigned int vlen,
                     int flags,
                     uint64_t timeout_gva);

/* Netlink emulation. */

/* Initialize netlink subsystem. Called from syscall_init(). */
void netlink_init(void);

/* Create a synthetic netlink socket fd. Returns guest fd, or negative errno.
 * Only NETLINK_ROUTE is supported; others return -EAFNOSUPPORT.
 */
int64_t netlink_socket(int protocol, int type);

/* Netlink bind (always succeeds for NETLINK_ROUTE). */
int64_t netlink_bind(int guest_fd,
                     guest_t *g,
                     uint64_t addr_gva,
                     uint32_t addrlen);

/* Netlink sendmsg: parse the request and buffer a response. */
int64_t netlink_sendmsg(int guest_fd, guest_t *g, uint64_t msg_gva, int flags);

/* Netlink recvmsg: return buffered response data. */
int64_t netlink_recvmsg(int guest_fd, guest_t *g, uint64_t msg_gva, int flags);

/* Netlink read: return buffered response data without msghdr metadata. */
int64_t netlink_read(int guest_fd,
                     guest_t *g,
                     uint64_t buf_gva,
                     uint64_t count);

/* Clean up abstract socket filesystem entry for a fd being closed. */
void absock_unregister_fd(int guest_fd);

/* Get/set the abstract socket namespace identifier shared across fork IPC. */
uint64_t absock_get_namespace_id(void);
void absock_set_namespace_id(uint64_t namespace_id);
