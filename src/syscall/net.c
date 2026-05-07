/* Socket/networking syscalls
 *
 * Copyright 2026 elfuse contributors
 * Copyright 2025 Moritz Angermann, zw3rk pte. ltd.
 * SPDX-License-Identifier: Apache-2.0
 *
 * Translates Linux aarch64 socket syscalls into macOS equivalents.
 * Key differences handled:
 *   - Address families: Linux AF_INET6=10, macOS AF_INET6=30
 *   - sockaddr layout: macOS has sa_len byte, Linux does not
 *   - Socket type flags: Linux packs SOCK_NONBLOCK/SOCK_CLOEXEC into type
 *   - Socket options: SOL_SOCKET option constants differ
 */

#include <stdatomic.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "utils.h"

#include "debug/log.h"
#include <fcntl.h>
#include <errno.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/uio.h>

#include "syscall/internal.h"
#include "syscall/net.h"
#include "syscall/net-abi.h"
#include "syscall/net-absock.h"
#include "syscall/net-sockopt.h"
#include "syscall/proc.h"
#include "syscall/signal.h"

/* Syscall implementations. */

int64_t sys_socket(guest_t *g, int domain, int type, int protocol)
{
    (void) g;

    /* AF_NETLINK: synthetic emulation, no macOS equivalent */
    if (domain == LINUX_AF_NETLINK)
        return netlink_socket(protocol, type);

    int mac_domain = translate_af_to_mac(domain);
    int real_type = extract_sock_type(type);
    int nonblock = extract_sock_nonblock(type);
    int cloexec = extract_sock_cloexec(type);

    int fd = socket(mac_domain, real_type, protocol);
    if (fd < 0)
        return linux_errno();

    /* Apply SOCK_NONBLOCK and SOCK_CLOEXEC */
    if ((nonblock && fd_set_nonblock(fd) < 0) ||
        (cloexec && fd_set_cloexec(fd) < 0)) {
        close(fd);
        return linux_errno();
    }

    /* Suppress SIGPIPE on this socket (macOS-specific) */
    int one = 1;
    setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof(one));

    int gfd = fd_alloc(FD_SOCKET, fd, NULL);
    if (gfd < 0) {
        close(fd);
        return -LINUX_EMFILE;
    }

    int linux_flags = 0;
    if (cloexec)
        linux_flags |= LINUX_O_CLOEXEC;
    fd_table[gfd].linux_flags = linux_flags;
    net_socket_cache_init_defaults(gfd, domain, real_type);

    return gfd;
}

int64_t sys_socketpair(guest_t *g,
                       int domain,
                       int type,
                       int protocol,
                       uint64_t sv_gva)
{
    int mac_domain = translate_af_to_mac(domain);
    int real_type = extract_sock_type(type);
    int nonblock = extract_sock_nonblock(type);
    int cloexec = extract_sock_cloexec(type);

    int fds[2];
    if (socketpair(mac_domain, real_type, protocol, fds) < 0)
        return linux_errno();

    /* Apply flags */
    for (int i = 0; i < 2; i++) {
        if ((nonblock && fd_set_nonblock(fds[i]) < 0) ||
            (cloexec && fd_set_cloexec(fds[i]) < 0)) {
            close(fds[0]);
            close(fds[1]);
            return linux_errno();
        }
        int one = 1;
        setsockopt(fds[i], SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof(one));
    }

    int gfd0 = fd_alloc(FD_SOCKET, fds[0], NULL);
    if (gfd0 < 0) {
        close(fds[0]);
        close(fds[1]);
        return -LINUX_EMFILE;
    }
    int gfd1 = fd_alloc(FD_SOCKET, fds[1], NULL);
    if (gfd1 < 0) {
        fd_mark_closed(gfd0);
        close(fds[0]);
        close(fds[1]);
        return -LINUX_EMFILE;
    }

    int linux_flags = cloexec ? LINUX_O_CLOEXEC : 0;
    fd_table[gfd0].linux_flags = linux_flags;
    fd_table[gfd1].linux_flags = linux_flags;
    net_socket_cache_init_defaults(gfd0, domain, real_type);
    net_socket_cache_init_defaults(gfd1, domain, real_type);

    int32_t guest_fds[2] = {gfd0, gfd1};
    if (guest_write_small(g, sv_gva, guest_fds, sizeof(guest_fds)) < 0) {
        fd_mark_closed(gfd0);
        close(fds[0]);
        fd_mark_closed(gfd1);
        close(fds[1]);
        return -LINUX_EFAULT;
    }

    return 0;
}

int64_t sys_bind(guest_t *g, int fd, uint64_t addr_gva, uint32_t addrlen)
{
    /* Netlink sockets use synthetic fd; dispatch to netlink handler */
    if (RANGE_CHECK(fd, 0, FD_TABLE_SIZE) && fd_table[fd].type == FD_NETLINK)
        return netlink_bind(fd, g, addr_gva, addrlen);

    host_fd_ref_t host_ref;
    if (host_fd_ref_open(fd, &host_ref) < 0)
        return -LINUX_EBADF;

    uint8_t linux_sa[128];
    if (addrlen > sizeof(linux_sa)) {
        host_fd_ref_close(&host_ref);
        return -LINUX_EINVAL;
    }
    if (guest_read(g, addr_gva, linux_sa, addrlen) < 0) {
        host_fd_ref_close(&host_ref);
        return -LINUX_EFAULT;
    }

    struct sockaddr_storage mac_sa;
    int mac_len;

    /* Abstract Unix socket: rewrite to filesystem path */
    int absock_idx = -1;
    if (absock_is_abstract_unix(linux_sa, addrlen)) {
        absock_init_cleanup();
        int bind_len;
        absock_idx =
            absock_bind_prepare(linux_sa, addrlen, &mac_sa, fd, &bind_len);
        if (absock_idx == -2) {
            host_fd_ref_close(&host_ref);
            return -LINUX_EADDRINUSE;
        }
        if (absock_idx < 0) {
            host_fd_ref_close(&host_ref);
            return -LINUX_EINVAL;
        }
        mac_len = bind_len;
    } else {
        mac_len = linux_to_mac_sockaddr(linux_sa, addrlen, &mac_sa);
        if (mac_len < 0) {
            host_fd_ref_close(&host_ref);
            return -LINUX_EINVAL;
        }
    }

    if (bind(host_ref.fd, (struct sockaddr *) &mac_sa, (socklen_t) mac_len) <
        0) {
        if (absock_idx >= 0)
            absock_bind_rollback(absock_idx);
        host_fd_ref_close(&host_ref);
        return linux_errno();
    }
    if (absock_idx >= 0)
        absock_bind_commit(absock_idx);
    host_fd_ref_close(&host_ref);
    return 0;
}

int64_t sys_listen(int fd, int backlog)
{
    host_fd_ref_t host_ref;
    if (host_fd_ref_open(fd, &host_ref) < 0)
        return -LINUX_EBADF;

    if (listen(host_ref.fd, backlog) < 0) {
        host_fd_ref_close(&host_ref);
        return linux_errno();
    }
    host_fd_ref_close(&host_ref);
    net_socket_cache_set_index(fd, SOCK_OPT_ACCEPTCONN, 1);
    return 0;
}

/* Shared implementation for accept and accept4. */
static int64_t do_accept(guest_t *g,
                         int fd,
                         uint64_t addr_gva,
                         uint64_t addrlen_gva,
                         int nonblock,
                         int cloexec)
{
    host_fd_ref_t host_ref;
    if (host_fd_ref_open(fd, &host_ref) < 0)
        return -LINUX_EBADF;

    struct sockaddr_storage mac_sa;
    socklen_t mac_len = sizeof(mac_sa);

    int new_fd = accept(host_ref.fd, (struct sockaddr *) &mac_sa, &mac_len);
    host_fd_ref_close(&host_ref);
    if (new_fd < 0)
        return linux_errno();

    if ((nonblock && fd_set_nonblock(new_fd) < 0) ||
        (cloexec && fd_set_cloexec(new_fd) < 0)) {
        close(new_fd);
        return linux_errno();
    }

    int one = 1;
    setsockopt(new_fd, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof(one));

    int gfd = fd_alloc(FD_SOCKET, new_fd, NULL);
    if (gfd < 0) {
        close(new_fd);
        return -LINUX_EMFILE;
    }
    fd_table[gfd].linux_flags = cloexec ? LINUX_O_CLOEXEC : 0;
    net_socket_cache_init_accept(gfd);

    /* Write back peer address if requested. The accept has already succeeded
     * and gfd is valid; EFAULT here mirrors Linux kernel behavior (close the
     * new fd and return -EFAULT).
     */
    if (addr_gva) {
        if (!addrlen_gva) {
            close(new_fd);
            fd_mark_closed(gfd);
            return -LINUX_EFAULT;
        }
        uint32_t guest_addrlen;
        if (guest_read_small(g, addrlen_gva, &guest_addrlen,
                             sizeof(guest_addrlen)) < 0) {
            close(new_fd);
            fd_mark_closed(gfd);
            return -LINUX_EFAULT;
        }
        uint8_t linux_sa[128];
        int out_len =
            mac_to_linux_sockaddr((struct sockaddr *) &mac_sa, mac_len,
                                  linux_sa, (uint32_t) sizeof(linux_sa));
        if (out_len > 0) {
            uint32_t actual_len = (uint32_t) out_len;
            uint32_t write_len = actual_len;
            if (write_len > guest_addrlen)
                write_len = guest_addrlen;
            /* Write back actual (not truncated) length per Linux semantics. */
            if (guest_write_small(g, addr_gva, linux_sa, write_len) < 0 ||
                guest_write_small(g, addrlen_gva, &actual_len,
                                  sizeof(actual_len)) < 0) {
                close(new_fd);
                fd_mark_closed(gfd);
                return -LINUX_EFAULT;
            }
        }
    }

    return gfd;
}

int64_t sys_accept(guest_t *g, int fd, uint64_t addr_gva, uint64_t addrlen_gva)
{
    return do_accept(g, fd, addr_gva, addrlen_gva, 0, 0);
}

int64_t sys_accept4(guest_t *g,
                    int fd,
                    uint64_t addr_gva,
                    uint64_t addrlen_gva,
                    int flags)
{
    int nonblock = (flags & LINUX_SOCK_NONBLOCK) != 0;
    int cloexec = (flags & LINUX_SOCK_CLOEXEC) != 0;
    return do_accept(g, fd, addr_gva, addrlen_gva, nonblock, cloexec);
}

int64_t sys_connect(guest_t *g, int fd, uint64_t addr_gva, uint32_t addrlen)
{
    host_fd_ref_t host_ref;
    if (host_fd_ref_open(fd, &host_ref) < 0)
        return -LINUX_EBADF;

    uint8_t linux_sa[128];
    if (addrlen > sizeof(linux_sa)) {
        host_fd_ref_close(&host_ref);
        return -LINUX_EINVAL;
    }
    if (guest_read(g, addr_gva, linux_sa, addrlen) < 0) {
        host_fd_ref_close(&host_ref);
        return -LINUX_EFAULT;
    }

    struct sockaddr_storage mac_sa;
    int mac_len;

    /* Abstract Unix socket: rewrite to filesystem path */
    if (absock_is_abstract_unix(linux_sa, addrlen)) {
        mac_len = absock_rewrite_connect(linux_sa, addrlen, &mac_sa);
        if (mac_len < 0) {
            host_fd_ref_close(&host_ref);
            return -LINUX_ECONNREFUSED;
        }
    } else {
        mac_len = linux_to_mac_sockaddr(linux_sa, addrlen, &mac_sa);
        if (mac_len < 0) {
            host_fd_ref_close(&host_ref);
            return -LINUX_EINVAL;
        }
    }

    /* glibc probes UDP connect(addr, port 0) during getaddrinfo() source
     * address selection. Linux accepts this; macOS rejects it. Use discard
     * port 9 for the host-only probe so getsockname() can still discover the
     * selected local address.
     */
    int so_type = 0;
    socklen_t so_type_len = sizeof(so_type);
    if (sockaddr_has_zero_port(&mac_sa) &&
        getsockopt(host_ref.fd, SOL_SOCKET, SO_TYPE, &so_type, &so_type_len) ==
            0 &&
        so_type == SOCK_DGRAM) {
        struct sockaddr_storage probe_sa = mac_sa;
        sockaddr_set_port(&probe_sa, htons(9));
        if (connect(host_ref.fd, (struct sockaddr *) &probe_sa,
                    (socklen_t) mac_len) < 0) {
            host_fd_ref_close(&host_ref);
            return linux_errno();
        }
        host_fd_ref_close(&host_ref);
        return 0;
    }

    if (connect(host_ref.fd, (struct sockaddr *) &mac_sa, (socklen_t) mac_len) <
        0) {
        host_fd_ref_close(&host_ref);
        return linux_errno();
    }
    host_fd_ref_close(&host_ref);
    return 0;
}

int64_t sys_getsockname(guest_t *g,
                        int fd,
                        uint64_t addr_gva,
                        uint64_t addrlen_gva)
{
    host_fd_ref_t host_ref;
    if (host_fd_ref_open(fd, &host_ref) < 0)
        return -LINUX_EBADF;

    struct sockaddr_storage mac_sa;
    socklen_t mac_len = sizeof(mac_sa);

    if (getsockname(host_ref.fd, (struct sockaddr *) &mac_sa, &mac_len) < 0) {
        host_fd_ref_close(&host_ref);
        return linux_errno();
    }

    uint32_t guest_addrlen;
    if (guest_read_small(g, addrlen_gva, &guest_addrlen,
                         sizeof(guest_addrlen)) < 0) {
        host_fd_ref_close(&host_ref);
        return -LINUX_EFAULT;
    }

    /* Check if this is a filesystem socket backing an abstract socket */
    uint8_t linux_sa[128];
    if (mac_sa.ss_family == AF_UNIX) {
        struct sockaddr_un *sun = (struct sockaddr_un *) &mac_sa;
        uint8_t abs_name[108];
        uint32_t abs_len = 0;
        if (absock_reverse_lookup(sun->sun_path, abs_name, &abs_len)) {
            /* Reconstruct abstract Linux sockaddr */
            uint32_t total = 2 + 1 + abs_len;
            uint16_t fam = LINUX_AF_UNIX;
            memset(linux_sa, 0, sizeof(linux_sa));
            memcpy(linux_sa, &fam, 2);
            memcpy(linux_sa + 3, abs_name, abs_len);

            uint32_t actual_len = total, write_len = actual_len;
            if (write_len > guest_addrlen)
                write_len = guest_addrlen;
            if (guest_write_small(g, addr_gva, linux_sa, write_len) < 0 ||
                guest_write_small(g, addrlen_gva, &actual_len,
                                  sizeof(actual_len)) < 0) {
                host_fd_ref_close(&host_ref);
                return -LINUX_EFAULT;
            }
            host_fd_ref_close(&host_ref);
            return 0;
        }
    }

    int out_len = mac_to_linux_sockaddr((struct sockaddr *) &mac_sa, mac_len,
                                        linux_sa, (uint32_t) sizeof(linux_sa));
    if (out_len < 0) {
        host_fd_ref_close(&host_ref);
        return -LINUX_EINVAL;
    }

    uint32_t actual_len = (uint32_t) out_len, write_len = actual_len;
    if (write_len > guest_addrlen)
        write_len = guest_addrlen;
    if (guest_write_small(g, addr_gva, linux_sa, write_len) < 0) {
        host_fd_ref_close(&host_ref);
        return -LINUX_EFAULT;
    }
    /* Write back actual (not truncated) length per Linux semantics */
    if (guest_write_small(g, addrlen_gva, &actual_len, sizeof(actual_len)) <
        0) {
        host_fd_ref_close(&host_ref);
        return -LINUX_EFAULT;
    }

    host_fd_ref_close(&host_ref);
    return 0;
}

int64_t sys_getpeername(guest_t *g,
                        int fd,
                        uint64_t addr_gva,
                        uint64_t addrlen_gva)
{
    host_fd_ref_t host_ref;
    if (host_fd_ref_open(fd, &host_ref) < 0)
        return -LINUX_EBADF;

    struct sockaddr_storage mac_sa;
    socklen_t mac_len = sizeof(mac_sa);

    if (getpeername(host_ref.fd, (struct sockaddr *) &mac_sa, &mac_len) < 0) {
        host_fd_ref_close(&host_ref);
        return linux_errno();
    }

    uint32_t guest_addrlen;
    if (guest_read_small(g, addrlen_gva, &guest_addrlen,
                         sizeof(guest_addrlen)) < 0) {
        host_fd_ref_close(&host_ref);
        return -LINUX_EFAULT;
    }

    uint8_t linux_sa[128];
    int out_len = mac_to_linux_sockaddr((struct sockaddr *) &mac_sa, mac_len,
                                        linux_sa, (uint32_t) sizeof(linux_sa));
    if (out_len < 0) {
        host_fd_ref_close(&host_ref);
        return -LINUX_EINVAL;
    }

    uint32_t actual_len = (uint32_t) out_len, write_len = actual_len;
    if (write_len > guest_addrlen)
        write_len = guest_addrlen;
    if (guest_write_small(g, addr_gva, linux_sa, write_len) < 0) {
        host_fd_ref_close(&host_ref);
        return -LINUX_EFAULT;
    }
    /* Write back actual (not truncated) length per Linux semantics */
    if (guest_write_small(g, addrlen_gva, &actual_len, sizeof(actual_len)) <
        0) {
        host_fd_ref_close(&host_ref);
        return -LINUX_EFAULT;
    }

    host_fd_ref_close(&host_ref);
    return 0;
}

int64_t sys_sendto(guest_t *g,
                   int fd,
                   uint64_t buf_gva,
                   uint64_t len,
                   int linux_flags,
                   uint64_t dest_gva,
                   uint32_t addrlen)
{
    host_fd_ref_t host_ref;
    if (host_fd_ref_open(fd, &host_ref) < 0)
        return -LINUX_EBADF;

    uint64_t avail = 0;
    void *buf =
        len > 0 ? guest_ptr_bound(g, buf_gva, &avail, MEM_PERM_R, len) : NULL;
    if (!buf && len > 0) {
        host_fd_ref_close(&host_ref);
        return -LINUX_EFAULT;
    }
    if (len > avail)
        len = avail;

    int mac_flags = translate_msg_flags(linux_flags);
    /* MSG_NOSIGNAL (0x4000): suppress SIGPIPE on EPIPE.
     * macOS has no MSG_NOSIGNAL; elfuse handles it by not queuing SIGPIPE.
     */
    int suppress_sigpipe = (linux_flags & 0x4000);

    if (dest_gva && addrlen > 0) {
        uint8_t linux_sa[128];
        if (addrlen > sizeof(linux_sa)) {
            host_fd_ref_close(&host_ref);
            return -LINUX_EINVAL;
        }
        if (guest_read(g, dest_gva, linux_sa, addrlen) < 0) {
            host_fd_ref_close(&host_ref);
            return -LINUX_EFAULT;
        }

        struct sockaddr_storage mac_sa;
        int mac_len = linux_to_mac_sockaddr(linux_sa, addrlen, &mac_sa);
        if (mac_len < 0) {
            host_fd_ref_close(&host_ref);
            return -LINUX_EINVAL;
        }

        ssize_t ret = sendto(host_ref.fd, buf, len, mac_flags,
                             (struct sockaddr *) &mac_sa, (socklen_t) mac_len);
        host_fd_ref_close(&host_ref);
        if (ret < 0) {
            if (errno == EPIPE && !suppress_sigpipe)
                signal_queue(LINUX_SIGPIPE);
            return linux_errno();
        }
        return ret;
    } else {
        ssize_t ret = send(host_ref.fd, buf, len, mac_flags);
        host_fd_ref_close(&host_ref);
        if (ret < 0) {
            if (errno == EPIPE && !suppress_sigpipe)
                signal_queue(LINUX_SIGPIPE);
            return linux_errno();
        }
        return ret;
    }
}

int64_t sys_recvfrom(guest_t *g,
                     int fd,
                     uint64_t buf_gva,
                     uint64_t len,
                     int flags,
                     uint64_t src_gva,
                     uint64_t addrlen_gva)
{
    host_fd_ref_t host_ref;
    if (host_fd_ref_open(fd, &host_ref) < 0)
        return -LINUX_EBADF;

    uint64_t avail = 0;
    void *buf =
        len > 0 ? guest_ptr_bound(g, buf_gva, &avail, MEM_PERM_W, len) : NULL;
    if (!buf && len > 0) {
        host_fd_ref_close(&host_ref);
        return -LINUX_EFAULT;
    }
    if (len > avail)
        len = avail;

    int mac_flags = translate_msg_flags(flags);

    struct sockaddr_storage mac_sa;
    socklen_t mac_len = sizeof(mac_sa);

    ssize_t ret;
    if (src_gva && addrlen_gva) {
        ret = recvfrom(host_ref.fd, buf, len, mac_flags,
                       (struct sockaddr *) &mac_sa, &mac_len);
    } else {
        ret = recv(host_ref.fd, buf, len, mac_flags);
    }
    if (ret < 0) {
        host_fd_ref_close(&host_ref);
        return linux_errno();
    }

    /* Write back source address if requested. */
    if (src_gva) {
        if (!addrlen_gva) {
            host_fd_ref_close(&host_ref);
            return -LINUX_EFAULT;
        }
        uint32_t guest_addrlen;
        if (guest_read_small(g, addrlen_gva, &guest_addrlen,
                             sizeof(guest_addrlen)) < 0) {
            host_fd_ref_close(&host_ref);
            return -LINUX_EFAULT;
        }
        uint8_t linux_sa[128];
        int out_len =
            mac_to_linux_sockaddr((struct sockaddr *) &mac_sa, mac_len,
                                  linux_sa, (uint32_t) sizeof(linux_sa));
        if (out_len > 0) {
            uint32_t actual_len = (uint32_t) out_len;
            uint32_t write_len = actual_len;
            if (write_len > guest_addrlen)
                write_len = guest_addrlen;
            if (guest_write_small(g, src_gva, linux_sa, write_len) < 0) {
                host_fd_ref_close(&host_ref);
                return -LINUX_EFAULT;
            }
            /* Write back actual length (Linux returns full size even if the
             * address was truncated to fit the buffer).
             */
            if (guest_write_small(g, addrlen_gva, &actual_len,
                                  sizeof(actual_len)) < 0) {
                host_fd_ref_close(&host_ref);
                return -LINUX_EFAULT;
            }
        }
    }

    host_fd_ref_close(&host_ref);
    return ret;
}

int64_t sys_setsockopt(guest_t *g,
                       int fd,
                       int level,
                       int optname,
                       uint64_t optval_gva,
                       uint32_t optlen)
{
    int small_int_opt = socket_opt_uses_small_int(level, optname);

    /* SO_PASSCRED: emulated entirely in elfuse. Cache the flag value so
     * getsockopt returns it and recvmsg knows to inject SCM_CREDENTIALS.
     * macOS has no equivalent. Do not forward.
     */
    if (level == LINUX_SOL_SOCKET && optname == LINUX_SO_PASSCRED) {
        if (!net_socket_fd_is_valid(fd))
            return -LINUX_EBADF;
        int value = 0;
        if (optlen > 0 && optlen <= sizeof(int)) {
            if (guest_read_small(g, optval_gva, &value, optlen) < 0)
                return -LINUX_EFAULT;
        }
        net_socket_cached_int_set(fd, LINUX_SOL_SOCKET, LINUX_SO_PASSCRED,
                                  value);
        return 0;
    }

    if (level == LINUX_IPPROTO_IP &&
        (optname == LINUX_IP_MTU_DISCOVER || optname == LINUX_IP_RECVERR)) {
        if (!net_socket_fd_is_valid(fd))
            return -LINUX_EBADF;
        if (optlen > sizeof(int))
            return -LINUX_EINVAL;
        if (optlen > 0) {
            int value = 0;
            if (guest_read_small(g, optval_gva, &value, optlen) < 0)
                return -LINUX_EFAULT;
            (void) value;
        }
        return 0;
    }

    if (optlen <= sizeof(int) && small_int_opt) {
        int value = 0;
        if (optlen > 0 && guest_read_small(g, optval_gva, &value, optlen) < 0)
            return -LINUX_EFAULT;
        value = socket_small_int_normalize(level, optname, value);

        int cached_value = 0;
        if (net_socket_cached_int_get(fd, level, optname, &cached_value) &&
            cached_value == value)
            return 0;
    }

    host_fd_ref_t host_ref;
    if (host_fd_ref_open(fd, &host_ref) < 0)
        return -LINUX_EBADF;

    int mac_level = level, mac_optname = optname;

    if (small_int_opt &&
        translate_small_int_sockopt(level, optname, &mac_level, &mac_optname)) {
        goto setsockopt_translated;
    }

    if (level == LINUX_SOL_SOCKET) {
        mac_level = SOL_SOCKET;
        mac_optname = translate_sockopt(optname);
        if (mac_optname < 0) {
            host_fd_ref_close(&host_ref);
            return -LINUX_ENOPROTOOPT;
        }
    } else if (level == LINUX_IPPROTO_TCP) {
        mac_level = IPPROTO_TCP;
        switch (optname) {
        case LINUX_TCP_NODELAY:
            mac_optname = TCP_NODELAY;
            break;
        case LINUX_TCP_KEEPIDLE:
            mac_optname = TCP_KEEPALIVE;
            break;
        case LINUX_TCP_KEEPINTVL:
            mac_optname = 0x101;
            break; /* TCP_KEEPINTVL */
        case LINUX_TCP_KEEPCNT:
            mac_optname = 0x102;
            break; /* TCP_KEEPCNT */
        default:
            break;
        }
    } else if (level == LINUX_IPPROTO_IP) {
        mac_level = IPPROTO_IP;
        mac_optname = translate_ip_sockopt_to_mac(optname);
        /* IP_MTU_DISCOVER and IP_RECVERR are handled by the early return
         * above (before host_fd_ref_open), so they never reach here.
         */
        if (mac_optname < 0) {
            host_fd_ref_close(&host_ref);
            return -LINUX_ENOPROTOOPT;
        }
    } else if (level == LINUX_IPPROTO_IPV6) {
        mac_level = IPPROTO_IPV6;
        if (optname == LINUX_IPV6_V6ONLY)
            mac_optname = IPV6_V6ONLY;
    }

setsockopt_translated:
    if (optlen <= sizeof(int) && small_int_opt) {
        int value = 0;
        if (optlen > 0 && guest_read_small(g, optval_gva, &value, optlen) < 0) {
            host_fd_ref_close(&host_ref);
            return -LINUX_EFAULT;
        }
        value = socket_small_int_normalize(level, optname, value);

        if ((level == LINUX_SOL_SOCKET &&
             (optname == LINUX_SO_KEEPALIVE || optname == LINUX_SO_REUSEADDR ||
              optname == LINUX_SO_DONTROUTE ||
              optname == LINUX_SO_OOBINLINE)) ||
            (level == LINUX_IPPROTO_TCP && optname == LINUX_TCP_NODELAY)) {
            int cached_value = 0;
            if (net_socket_cached_int_get(fd, level, optname, &cached_value) &&
                cached_value == value) {
                host_fd_ref_close(&host_ref);
                return 0;
            }
        }

        if (setsockopt(host_ref.fd, mac_level, mac_optname, &value,
                       (socklen_t) optlen) < 0) {
            log_debug("setsockopt(fd=%d, level=%d/%d, opt=%d/%d, len=%u): %s",
                      fd, level, mac_level, optname, mac_optname, optlen,
                      strerror(errno));
            host_fd_ref_close(&host_ref);
            return linux_errno();
        }
        net_socket_cached_int_set(fd, level, optname, value);
        host_fd_ref_close(&host_ref);
        return 0;
    }

    uint8_t optval[256];
    if (optlen > sizeof(optval)) {
        host_fd_ref_close(&host_ref);
        return -LINUX_EINVAL;
    }
    if (optlen > 0 && guest_read(g, optval_gva, optval, optlen) < 0) {
        host_fd_ref_close(&host_ref);
        return -LINUX_EFAULT;
    }

    if (setsockopt(host_ref.fd, mac_level, mac_optname, optval,
                   (socklen_t) optlen) < 0) {
        log_debug("setsockopt(fd=%d, level=%d/%d, opt=%d/%d, len=%u): %s", fd,
                  level, mac_level, optname, mac_optname, optlen,
                  strerror(errno));
        host_fd_ref_close(&host_ref);
        return linux_errno();
    }
    host_fd_ref_close(&host_ref);
    return 0;
}

int64_t sys_getsockopt(guest_t *g,
                       int fd,
                       int level,
                       int optname,
                       uint64_t optval_gva,
                       uint64_t optlen_gva)
{
    uint32_t guest_optlen;
    if (guest_read_small(g, optlen_gva, &guest_optlen, sizeof(guest_optlen)) <
        0)
        return -LINUX_EFAULT;

    /* SO_PEERCRED: synthesize struct ucred from guest identity.
     * macOS has LOCAL_PEERCRED but returns a different struct; we fabricate
     * the Linux ucred with the guest's own PID/UID/GID, which is correct
     * for AF_UNIX sockets within the same elfuse instance.
     */
    if (level == LINUX_SOL_SOCKET && optname == LINUX_SO_PEERCRED) {
        if (!net_socket_fd_is_valid(fd))
            return -LINUX_EBADF;
        linux_ucred_t cred = {
            .pid = (int32_t) proc_get_pid(),
            .uid = proc_get_uid(),
            .gid = proc_get_gid(),
        };
        uint32_t out_len = sizeof(cred);
        if (guest_optlen < out_len)
            out_len = guest_optlen;
        if (out_len > 0 && guest_write_small(g, optval_gva, &cred, out_len) < 0)
            return -LINUX_EFAULT;
        uint32_t actual_len = sizeof(cred);
        if (guest_write_small(g, optlen_gva, &actual_len, sizeof(actual_len)) <
            0)
            return -LINUX_EFAULT;
        return 0;
    }

    if (level == LINUX_IPPROTO_IP &&
        (optname == LINUX_IP_MTU_DISCOVER || optname == LINUX_IP_RECVERR)) {
        if (!net_socket_fd_is_valid(fd))
            return -LINUX_EBADF;
        if (guest_optlen >= sizeof(int)) {
            int val = 1;
            uint32_t out_len = sizeof(int);
            if (guest_write_small(g, optval_gva, &val, sizeof(val)) < 0)
                return -LINUX_EFAULT;
            if (guest_write_small(g, optlen_gva, &out_len, sizeof(out_len)) < 0)
                return -LINUX_EFAULT;
        }
        return 0;
    }

    if (guest_optlen <= sizeof(int)) {
        int value = 0;
        if (net_socket_cached_int_get(fd, level, optname, &value)) {
            uint32_t actual_len = sizeof(int), write_len = actual_len;
            if (write_len > guest_optlen)
                write_len = guest_optlen;
            if (guest_write_small(g, optval_gva, &value, write_len) < 0)
                return -LINUX_EFAULT;
            if (guest_write_small(g, optlen_gva, &actual_len,
                                  sizeof(actual_len)) < 0)
                return -LINUX_EFAULT;
            return 0;
        }
    }

    host_fd_ref_t host_ref;
    if (host_fd_ref_open(fd, &host_ref) < 0)
        return -LINUX_EBADF;

    int mac_level = level, mac_optname = optname;
    int small_int_opt = socket_opt_uses_small_int(level, optname);

    if (small_int_opt &&
        translate_small_int_sockopt(level, optname, &mac_level, &mac_optname)) {
        goto getsockopt_translated;
    }

    if (level == LINUX_SOL_SOCKET) {
        mac_level = SOL_SOCKET;
        mac_optname = translate_sockopt(optname);
        if (mac_optname < 0) {
            host_fd_ref_close(&host_ref);
            return -LINUX_ENOPROTOOPT;
        }
    } else if (level == LINUX_IPPROTO_TCP) {
        mac_level = IPPROTO_TCP;
        switch (optname) {
        case LINUX_TCP_NODELAY:
            mac_optname = TCP_NODELAY;
            break;
        case LINUX_TCP_KEEPIDLE:
            mac_optname = TCP_KEEPALIVE;
            break;
        case LINUX_TCP_KEEPINTVL:
            mac_optname = 0x101;
            break;
        case LINUX_TCP_KEEPCNT:
            mac_optname = 0x102;
            break;
        default:
            break;
        }
    } else if (level == LINUX_IPPROTO_IP) {
        mac_level = IPPROTO_IP;
        mac_optname = translate_ip_sockopt_to_mac(optname);
        /* IP_MTU_DISCOVER and IP_RECVERR are handled by the early return
         * above (before host_fd_ref_open), so they never reach here.
         */
        if (mac_optname < 0) {
            host_fd_ref_close(&host_ref);
            return -LINUX_ENOPROTOOPT;
        }
    } else if (level == LINUX_IPPROTO_IPV6) {
        mac_level = IPPROTO_IPV6;
        if (optname == LINUX_IPV6_V6ONLY)
            mac_optname = IPV6_V6ONLY;
    }

getsockopt_translated:
    if (guest_read_small(g, optlen_gva, &guest_optlen, sizeof(guest_optlen)) <
        0) {
        host_fd_ref_close(&host_ref);
        return -LINUX_EFAULT;
    }

    if (guest_optlen <= sizeof(int) && small_int_opt) {
        int value = 0;
        socklen_t mac_optlen = sizeof(value);
        uint32_t actual_len, write_len;
        int used_cache = net_socket_cached_int_get(fd, level, optname, &value);

        if (!used_cache) {
            if (getsockopt(host_ref.fd, mac_level, mac_optname, &value,
                           &mac_optlen) < 0) {
                host_fd_ref_close(&host_ref);
                return linux_errno();
            }

            if (level == LINUX_SOL_SOCKET && optname == LINUX_SO_TYPE)
                value &= 0xF;

            if (level == LINUX_SOL_SOCKET && optname == LINUX_SO_ERROR &&
                value != 0) {
                errno = value;
                value = (int) (-linux_errno());
            }

            net_socket_cached_int_set(fd, level, optname, value);
        }

        actual_len = used_cache ? sizeof(int) : (uint32_t) mac_optlen;
        write_len = actual_len;
        if (write_len > guest_optlen)
            write_len = guest_optlen;
        if (write_len > 0 &&
            guest_write_small(g, optval_gva, &value, write_len) < 0) {
            host_fd_ref_close(&host_ref);
            return -LINUX_EFAULT;
        }
        if (guest_write_small(g, optlen_gva, &actual_len, sizeof(actual_len)) <
            0) {
            host_fd_ref_close(&host_ref);
            return -LINUX_EFAULT;
        }

        host_fd_ref_close(&host_ref);
        return 0;
    }

    uint8_t optval[256];
    socklen_t mac_optlen = (socklen_t) guest_optlen;
    if (mac_optlen > sizeof(optval))
        mac_optlen = sizeof(optval);

    if (getsockopt(host_ref.fd, mac_level, mac_optname, optval, &mac_optlen) <
        0) {
        host_fd_ref_close(&host_ref);
        return linux_errno();
    }

    /* SO_TYPE: macOS returns the raw socket type. On Linux, getsockopt
     * SO_TYPE returns the base type without SOCK_NONBLOCK/SOCK_CLOEXEC
     * flags, and the numeric values happen to match (SOCK_STREAM=1,
     * SOCK_DGRAM=2, SOCK_RAW=3). Strip any flag bits for safety.
     */
    if (level == LINUX_SOL_SOCKET && optname == LINUX_SO_TYPE &&
        mac_optlen >= (socklen_t) sizeof(int)) {
        int *type_val = (int *) optval;
        *type_val &= 0xF; /* Keep only the base socket type */
    }

    /* SO_ERROR: macOS returns a macOS errno value; translate to Linux. */
    if (level == LINUX_SOL_SOCKET && optname == LINUX_SO_ERROR &&
        mac_optlen >= (socklen_t) sizeof(int)) {
        int *err_val = (int *) optval;
        if (*err_val != 0) {
            errno = *err_val;
            *err_val = (int) (-linux_errno());
        }
    }

    /* Write option value, truncating to guest buffer size if needed.
     * Write back actual length (not truncated) per Linux semantics: Linux
     * getsockopt returns the real option size so the caller can detect
     * truncation and retry with a larger buffer.
     */
    uint32_t actual_len = (uint32_t) mac_optlen, write_len = actual_len;
    if (write_len > guest_optlen)
        write_len = guest_optlen;
    if (guest_write_small(g, optval_gva, optval, write_len) < 0) {
        host_fd_ref_close(&host_ref);
        return -LINUX_EFAULT;
    }
    if (guest_write_small(g, optlen_gva, &actual_len, sizeof(actual_len)) < 0) {
        host_fd_ref_close(&host_ref);
        return -LINUX_EFAULT;
    }

    host_fd_ref_close(&host_ref);
    return 0;
}

int64_t sys_shutdown(int fd, int how)
{
    host_fd_ref_t host_ref;
    if (host_fd_ref_open(fd, &host_ref) < 0)
        return -LINUX_EBADF;

    /* Shutdown constants are identical on Linux and macOS */
    if (shutdown(host_ref.fd, how) < 0) {
        host_fd_ref_close(&host_ref);
        return linux_errno();
    }
    host_fd_ref_close(&host_ref);
    return 0;
}
