/* Socket message syscalls
 *
 * Copyright 2026 elfuse contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <unistd.h>

#include "utils.h"

#include "syscall/internal.h"
#include "syscall/net.h"
#include "syscall/net-sockopt.h"
#include "syscall/net-abi.h"
#include "syscall/proc.h"
#include "syscall/signal.h"

/* Linux SCM_MAX_FD: maximum number of file descriptors in SCM_RIGHTS */
#define LINUX_SCM_MAX_FD 253

static int translate_scm_rights_fds(int *fds, size_t nfds)
{
    if (nfds > LINUX_SCM_MAX_FD)
        return -LINUX_EINVAL;

    int stack_host_fds[16];
    int *host_fds = stack_host_fds;
    int *heap_host_fds = NULL;
    int seen_guest_fds[128], seen_host_fds[128];
    size_t seen_count = 0;

    if (nfds > ARRAY_SIZE(stack_host_fds)) {
        heap_host_fds = malloc(nfds * sizeof(*heap_host_fds));
        if (!heap_host_fds)
            return -LINUX_ENOMEM;
        host_fds = heap_host_fds;
    }

    for (size_t i = 0; i < nfds; i++) {
        int guest_fd = fds[i], host_fd = -1;

        for (size_t j = 0; j < seen_count; j++) {
            if (seen_guest_fds[j] == guest_fd) {
                host_fd = seen_host_fds[j];
                break;
            }
        }

        if (host_fd < 0) {
            host_fd = fd_to_host(guest_fd);
            if (seen_count < ARRAY_SIZE(seen_guest_fds)) {
                seen_guest_fds[seen_count] = guest_fd;
                seen_host_fds[seen_count] = host_fd;
                seen_count++;
            }
        }
        if (host_fd < 0) {
            free(heap_host_fds);
            return -LINUX_EBADF;
        }
        host_fds[i] = host_fd;
    }

    for (size_t i = 0; i < nfds; i++)
        fds[i] = host_fds[i];

    free(heap_host_fds);
    return 0;
}

static void recvmsg_cleanup_scm_rights(const int *scm_gfds,
                                       const int *scm_hfds,
                                       int scm_nfds)
{
    for (int i = 0; i < scm_nfds; i++) {
        fd_mark_closed(scm_gfds[i]);
        close(scm_hfds[i]);
    }
}

static void recvmsg_close_host_rights(const void *data_src, size_t data_len)
{
    size_t nfds = data_len / sizeof(int);
    const int *fds = (const int *) data_src;
    for (size_t i = 0; i < nfds; i++)
        close(fds[i]);
}

int64_t sys_sendmsg(guest_t *g, int fd, uint64_t msg_gva, int linux_flags)
{
    if (RANGE_CHECK(fd, 0, FD_TABLE_SIZE) && fd_table[fd].type == FD_NETLINK)
        return netlink_sendmsg(fd, g, msg_gva, linux_flags);

    host_fd_ref_t host_ref;
    if (host_fd_ref_open(fd, &host_ref) < 0)
        return -LINUX_EBADF;

    linux_msghdr_t lmsg;
    if (guest_read_small(g, msg_gva, &lmsg, sizeof(lmsg)) < 0) {
        host_fd_ref_close(&host_ref);
        return -LINUX_EFAULT;
    }

    int mac_flags = translate_msg_flags(linux_flags);
    bool suppress_sigpipe = (linux_flags & 0x4000) != 0;

    if ((!lmsg.msg_name || lmsg.msg_namelen == 0) &&
        (!lmsg.msg_control || lmsg.msg_controllen == 0) &&
        lmsg.msg_iovlen == 1) {
        struct {
            uint64_t iov_base, iov_len;
        } guest_iov;

        if (guest_read_small(g, lmsg.msg_iov, &guest_iov, sizeof(guest_iov)) <
            0) {
            host_fd_ref_close(&host_ref);
            return -LINUX_EFAULT;
        }

        void *base = NULL;
        size_t len = 0;
        if (guest_iov.iov_len > 0) {
            uint64_t avail = 0;
            base = guest_ptr_bound(g, guest_iov.iov_base, &avail, MEM_PERM_R,
                                   guest_iov.iov_len);
            if (!base) {
                host_fd_ref_close(&host_ref);
                return -LINUX_EFAULT;
            }
            len = guest_iov.iov_len;
            if (len > avail)
                len = (size_t) avail;
        }

        ssize_t ret = send(host_ref.fd, base, len, mac_flags);
        host_fd_ref_close(&host_ref);
        if (ret < 0) {
            if (errno == EPIPE && !suppress_sigpipe)
                signal_queue(LINUX_SIGPIPE);
            return linux_errno();
        }
        return ret;
    }

    struct sockaddr_storage mac_sa;
    struct sockaddr *dest_sa = NULL;
    socklen_t dest_len = 0;
    if (lmsg.msg_name && lmsg.msg_namelen > 0) {
        uint8_t linux_sa[128];
        if (lmsg.msg_namelen > sizeof(linux_sa)) {
            host_fd_ref_close(&host_ref);
            return -LINUX_EINVAL;
        }
        if (guest_read(g, lmsg.msg_name, linux_sa, lmsg.msg_namelen) < 0) {
            host_fd_ref_close(&host_ref);
            return -LINUX_EFAULT;
        }
        int ml = linux_to_mac_sockaddr(linux_sa, lmsg.msg_namelen, &mac_sa);
        if (ml < 0) {
            host_fd_ref_close(&host_ref);
            return -LINUX_EINVAL;
        }
        dest_sa = (struct sockaddr *) &mac_sa;
        dest_len = (socklen_t) ml;
    }

    if (lmsg.msg_iovlen > 64) {
        host_fd_ref_close(&host_ref);
        return -LINUX_EINVAL;
    }

    struct {
        uint64_t iov_base, iov_len;
    } guest_iov[64];

    if (lmsg.msg_iovlen > 0) {
        if (guest_read(g, lmsg.msg_iov, guest_iov, lmsg.msg_iovlen * 16) < 0) {
            host_fd_ref_close(&host_ref);
            return -LINUX_EFAULT;
        }
    }

    struct iovec host_iov[64];
    for (uint64_t i = 0; i < lmsg.msg_iovlen; i++) {
        if (guest_iov[i].iov_len == 0) {
            host_iov[i].iov_base = NULL;
            host_iov[i].iov_len = 0;
            continue;
        }
        uint64_t avail = 0;
        void *base = guest_ptr_bound(g, guest_iov[i].iov_base, &avail,
                                     MEM_PERM_R, guest_iov[i].iov_len);
        if (!base) {
            host_fd_ref_close(&host_ref);
            return -LINUX_EFAULT;
        }
        uint64_t len = guest_iov[i].iov_len;
        if (len > avail)
            len = avail;
        host_iov[i].iov_base = base;
        host_iov[i].iov_len = len;
    }

    uint8_t linux_ctrl_stack[512], mac_ctrl_stack[512];
    uint8_t *linux_ctrl = linux_ctrl_stack;
    uint8_t *mac_ctrl = mac_ctrl_stack;
    size_t mac_ctrl_size = sizeof(mac_ctrl_stack);
    uint8_t *linux_ctrl_heap = NULL;
    uint8_t *mac_ctrl_heap = NULL;
    uint8_t *ctrl_ptr = NULL;
    socklen_t ctrl_len = 0;

    if (lmsg.msg_control && lmsg.msg_controllen > 0) {
        size_t clen = lmsg.msg_controllen;
        if (clen > 65536) {
            host_fd_ref_close(&host_ref);
            return -LINUX_EINVAL;
        }
        if (clen > sizeof(linux_ctrl_stack)) {
            linux_ctrl_heap = malloc(clen);
            mac_ctrl_heap = malloc(clen);
            if (!linux_ctrl_heap || !mac_ctrl_heap) {
                free(linux_ctrl_heap);
                free(mac_ctrl_heap);
                host_fd_ref_close(&host_ref);
                return -LINUX_ENOMEM;
            }
            linux_ctrl = linux_ctrl_heap;
            mac_ctrl = mac_ctrl_heap;
            mac_ctrl_size = clen;
        }
        if (guest_read(g, lmsg.msg_control, linux_ctrl, clen) < 0) {
            free(linux_ctrl_heap);
            free(mac_ctrl_heap);
            host_fd_ref_close(&host_ref);
            return -LINUX_EFAULT;
        }

        size_t lpos = 0;
        size_t mpos = 0;
        uint64_t lctl_len = lmsg.msg_controllen;

        while (lpos + 16 <= lctl_len) {
            uint64_t lcmsg_len;
            int32_t lcmsg_level, lcmsg_type;
            memcpy(&lcmsg_len, linux_ctrl + lpos, 8);
            memcpy(&lcmsg_level, linux_ctrl + lpos + 8, 4);
            memcpy(&lcmsg_type, linux_ctrl + lpos + 12, 4);

            if (lcmsg_len < 16)
                break;
            if (lcmsg_len > lctl_len - lpos)
                break;
            size_t ldata_len = (size_t) (lcmsg_len - 16);
            size_t next_lpos = lpos + (size_t) ((lcmsg_len + 7) & ~7ULL);

            if (lcmsg_level == 1 && lcmsg_type == LINUX_SCM_CREDENTIALS) {
                lpos = next_lpos;
                continue;
            }

            int mac_level = (lcmsg_level == 1) ? SOL_SOCKET : lcmsg_level;
            int mac_type = lcmsg_type;
            if (lcmsg_level == LINUX_IPPROTO_IP) {
                mac_level = IPPROTO_IP;
                mac_type = translate_ip_cmsg_to_mac(lcmsg_type);
                if (mac_type < 0) {
                    lpos = next_lpos;
                    continue;
                }
            }

            if (mpos + CMSG_SPACE(ldata_len) > mac_ctrl_size)
                break;

            struct msghdr tmsg = {.msg_control = mac_ctrl + mpos,
                                  .msg_controllen = mac_ctrl_size - mpos};
            struct cmsghdr *cmsg = CMSG_FIRSTHDR(&tmsg);
            if (!cmsg)
                break;
            cmsg->cmsg_len = CMSG_LEN(ldata_len);
            cmsg->cmsg_level = mac_level;
            cmsg->cmsg_type = mac_type;
            memcpy(CMSG_DATA(cmsg), linux_ctrl + lpos + 16, ldata_len);

            if (mac_level == SOL_SOCKET && mac_type == SCM_RIGHTS) {
                int *fds = (int *) CMSG_DATA(cmsg);
                size_t nfds = ldata_len / sizeof(int);
                int rc = translate_scm_rights_fds(fds, nfds);
                if (rc < 0) {
                    free(linux_ctrl_heap);
                    free(mac_ctrl_heap);
                    host_fd_ref_close(&host_ref);
                    return rc;
                }
            }

            mpos += CMSG_SPACE(ldata_len);
            lpos = next_lpos;
        }

        if (mpos > 0) {
            ctrl_ptr = mac_ctrl;
            ctrl_len = (socklen_t) mpos;
        }
    }

    struct msghdr msg = {
        .msg_name = dest_sa,
        .msg_namelen = dest_len,
        .msg_iov = host_iov,
        .msg_iovlen = (int) lmsg.msg_iovlen,
        .msg_control = ctrl_ptr,
        .msg_controllen = ctrl_len,
        .msg_flags = 0,
    };

    ssize_t ret = sendmsg(host_ref.fd, &msg, mac_flags);
    free(linux_ctrl_heap);
    free(mac_ctrl_heap);
    host_fd_ref_close(&host_ref);
    if (ret < 0) {
        if (errno == EPIPE && !suppress_sigpipe)
            signal_queue(LINUX_SIGPIPE);
        return linux_errno();
    }
    return ret;
}

int64_t sys_recvmsg(guest_t *g, int fd, uint64_t msg_gva, int flags)
{
    if (RANGE_CHECK(fd, 0, FD_TABLE_SIZE) && fd_table[fd].type == FD_NETLINK)
        return netlink_recvmsg(fd, g, msg_gva, flags);

    host_fd_ref_t host_ref;
    if (host_fd_ref_open(fd, &host_ref) < 0)
        return -LINUX_EBADF;

    linux_msghdr_t lmsg;
    if (guest_read_small(g, msg_gva, &lmsg, sizeof(lmsg)) < 0) {
        host_fd_ref_close(&host_ref);
        return -LINUX_EFAULT;
    }

    int mac_flags = translate_msg_flags(flags);

    if (!lmsg.msg_name && (!lmsg.msg_control || lmsg.msg_controllen == 0) &&
        lmsg.msg_iovlen == 1) {
        struct {
            uint64_t iov_base, iov_len;
        } guest_iov;

        if (guest_read_small(g, lmsg.msg_iov, &guest_iov, sizeof(guest_iov)) <
            0) {
            host_fd_ref_close(&host_ref);
            return -LINUX_EFAULT;
        }

        void *base = NULL;
        size_t len = 0;
        if (guest_iov.iov_len > 0) {
            uint64_t avail = 0;
            base = guest_ptr_bound(g, guest_iov.iov_base, &avail, MEM_PERM_W,
                                   guest_iov.iov_len);
            if (!base) {
                host_fd_ref_close(&host_ref);
                return -LINUX_EFAULT;
            }
            len = guest_iov.iov_len;
            if (len > avail)
                len = (size_t) avail;
        }

        struct iovec host_iov = {
            .iov_base = base,
            .iov_len = len,
        };
        struct msghdr msg = {
            .msg_iov = &host_iov,
            .msg_iovlen = 1,
            .msg_flags = 0,
        };

        ssize_t ret = recvmsg(host_ref.fd, &msg, mac_flags);
        if (ret < 0) {
            host_fd_ref_close(&host_ref);
            return linux_errno();
        }
        uint64_t zero64 = 0;
        int32_t mflags = mac_to_linux_msg_flags(msg.msg_flags);
        if (guest_write_small(
                g, msg_gva + offsetof(linux_msghdr_t, msg_controllen), &zero64,
                sizeof(zero64)) < 0 ||
            guest_write_small(g, msg_gva + offsetof(linux_msghdr_t, msg_flags),
                              &mflags, sizeof(mflags)) < 0) {
            host_fd_ref_close(&host_ref);
            return -LINUX_EFAULT;
        }
        host_fd_ref_close(&host_ref);
        return ret;
    }

    if (lmsg.msg_iovlen > 64) {
        host_fd_ref_close(&host_ref);
        return -LINUX_EINVAL;
    }

    struct {
        uint64_t iov_base, iov_len;
    } guest_iov[64];

    if (lmsg.msg_iovlen > 0) {
        if (guest_read(g, lmsg.msg_iov, guest_iov, lmsg.msg_iovlen * 16) < 0) {
            host_fd_ref_close(&host_ref);
            return -LINUX_EFAULT;
        }
    }

    struct iovec host_iov[64];
    for (uint64_t i = 0; i < lmsg.msg_iovlen; i++) {
        if (guest_iov[i].iov_len == 0) {
            host_iov[i].iov_base = NULL;
            host_iov[i].iov_len = 0;
            continue;
        }
        uint64_t avail = 0;
        void *base = guest_ptr_bound(g, guest_iov[i].iov_base, &avail,
                                     MEM_PERM_W, guest_iov[i].iov_len);
        if (!base) {
            host_fd_ref_close(&host_ref);
            return -LINUX_EFAULT;
        }
        uint64_t len = guest_iov[i].iov_len;
        if (len > avail)
            len = avail;
        host_iov[i].iov_base = base;
        host_iov[i].iov_len = len;
    }

    struct sockaddr_storage mac_sa;
    socklen_t sa_len = sizeof(mac_sa);

    if (lmsg.msg_controllen > 65536)
        lmsg.msg_controllen = 65536;

    uint8_t mac_ctrl_stack[256];
    uint8_t *mac_ctrl = mac_ctrl_stack;
    uint8_t *mac_ctrl_heap = NULL;
    socklen_t ctrl_alloc = 0;
    bool using_heap_ctrl = false;

    if (lmsg.msg_control && lmsg.msg_controllen > 0) {
        size_t mac_need = lmsg.msg_controllen;
        if (mac_need > sizeof(mac_ctrl_stack)) {
            mac_ctrl_heap = malloc(mac_need);
            if (mac_ctrl_heap) {
                mac_ctrl = mac_ctrl_heap;
                using_heap_ctrl = true;
            } else {
                mac_need = sizeof(mac_ctrl_stack);
            }
        }
        ctrl_alloc =
            (socklen_t) (using_heap_ctrl ? mac_need : sizeof(mac_ctrl_stack));
    }

    struct msghdr msg = {
        .msg_name = lmsg.msg_name ? &mac_sa : NULL,
        .msg_namelen = lmsg.msg_name ? sa_len : 0,
        .msg_iov = host_iov,
        .msg_iovlen = (int) lmsg.msg_iovlen,
        .msg_control = ctrl_alloc > 0 ? mac_ctrl : NULL,
        .msg_controllen = ctrl_alloc,
        .msg_flags = 0,
    };

    int scm_gfds[128];
    int scm_hfds[128];
    int scm_nfds = 0;

    ssize_t ret = recvmsg(host_ref.fd, &msg, mac_flags);
    if (ret < 0) {
        free(mac_ctrl_heap);
        host_fd_ref_close(&host_ref);
        return linux_errno();
    }

    if (lmsg.msg_name) {
        if (msg.msg_namelen > 0) {
            uint8_t linux_sa[128];
            int out_len = mac_to_linux_sockaddr((struct sockaddr *) &mac_sa,
                                                msg.msg_namelen, linux_sa,
                                                (uint32_t) sizeof(linux_sa));
            if (out_len > 0) {
                uint32_t write_len = (uint32_t) out_len;
                if (write_len > lmsg.msg_namelen)
                    write_len = lmsg.msg_namelen;
                if (guest_write_small(g, lmsg.msg_name, linux_sa, write_len) <
                    0) {
                    free(mac_ctrl_heap);
                    host_fd_ref_close(&host_ref);
                    return -LINUX_EFAULT;
                }
            }
        }
        uint32_t nl = (uint32_t) msg.msg_namelen;
        if (guest_write_small(g,
                              msg_gva + offsetof(linux_msghdr_t, msg_namelen),
                              &nl, sizeof(nl)) < 0) {
            free(mac_ctrl_heap);
            host_fd_ref_close(&host_ref);
            return -LINUX_EFAULT;
        }
    }

    int ctrl_truncated = 0;
    if (ctrl_alloc > 0 && msg.msg_controllen > 0 && lmsg.msg_control) {
        size_t lctrl_size =
            lmsg.msg_controllen > 512 ? lmsg.msg_controllen : 512;
        uint8_t lctrl_stack[512];
        uint8_t *linux_ctrl = lctrl_stack;
        uint8_t *lctrl_heap = NULL;
        if (lctrl_size > sizeof(lctrl_stack)) {
            lctrl_heap = malloc(lctrl_size);
            if (!lctrl_heap) {
                free(mac_ctrl_heap);
                host_fd_ref_close(&host_ref);
                return -LINUX_ENOMEM;
            }
            linux_ctrl = lctrl_heap;
        }
        size_t lpos = 0;

        for (struct cmsghdr *cmsg = CMSG_FIRSTHDR(&msg); cmsg;
             cmsg = CMSG_NXTHDR(&msg, cmsg)) {
            if (cmsg->cmsg_len < CMSG_LEN(0))
                continue;
            size_t data_len = cmsg->cmsg_len - CMSG_LEN(0);
            if (data_len > lctrl_size - 16) {
                ctrl_truncated = 1;
                break;
            }
            uint64_t lcmsg_len = 16 + data_len;
            size_t lcmsg_space = (size_t) ((lcmsg_len + 7) & ~7ULL);

            if (lpos + lcmsg_space > lctrl_size ||
                lpos + lcmsg_space > lmsg.msg_controllen) {
                ctrl_truncated = 1;
                break;
            }

            int32_t llevel =
                (cmsg->cmsg_level == SOL_SOCKET) ? 1 : cmsg->cmsg_level;
            int32_t ltype = cmsg->cmsg_type;
            if (cmsg->cmsg_level == IPPROTO_IP) {
                int translated = translate_ip_cmsg_to_linux(cmsg->cmsg_type);
                if (translated < 0)
                    continue;
                llevel = LINUX_IPPROTO_IP;
                ltype = translated;
            }

            uint8_t data_copy_stack[128];
            uint8_t *data_copy = data_copy_stack;
            uint8_t *data_copy_heap = NULL;
            uint8_t *data_src = CMSG_DATA(cmsg);
            if (cmsg->cmsg_level == SOL_SOCKET &&
                cmsg->cmsg_type == SCM_RIGHTS) {
                if (data_len > LINUX_SCM_MAX_FD * sizeof(int)) {
                    recvmsg_close_host_rights(data_src, data_len);
                    recvmsg_cleanup_scm_rights(scm_gfds, scm_hfds, scm_nfds);
                    free(lctrl_heap);
                    free(mac_ctrl_heap);
                    host_fd_ref_close(&host_ref);
                    return -LINUX_EINVAL;
                }
                if (data_len > sizeof(data_copy_stack)) {
                    data_copy_heap = malloc(data_len);
                    if (!data_copy_heap) {
                        recvmsg_close_host_rights(data_src, data_len);
                        break;
                    }
                    data_copy = data_copy_heap;
                }
                memcpy(data_copy, data_src, data_len);
                int *fds = (int *) data_copy;
                size_t nfds = data_len / sizeof(int);
                for (size_t i = 0; i < nfds; i++) {
                    int host_recv_fd = fds[i];
                    int gfd = fd_alloc(FD_REGULAR, fds[i], NULL);
                    if (gfd < 0) {
                        close(fds[i]);
                        fds[i] = -1;
                    } else {
                        fds[i] = gfd;
                        if (scm_nfds < (int) ARRAY_SIZE(scm_gfds)) {
                            scm_gfds[scm_nfds] = gfd;
                            scm_hfds[scm_nfds] = host_recv_fd;
                            scm_nfds++;
                        }
                        if (flags & 0x40000000)
                            fd_table[gfd].linux_flags |= LINUX_O_CLOEXEC;
                    }
                }
                data_src = data_copy;
            }

            memset(linux_ctrl + lpos, 0, lcmsg_space);
            memcpy(linux_ctrl + lpos, &lcmsg_len, 8);
            memcpy(linux_ctrl + lpos + 8, &llevel, 4);
            memcpy(linux_ctrl + lpos + 12, &ltype, 4);
            memcpy(linux_ctrl + lpos + 16, data_src, data_len);
            free(data_copy_heap);

            lpos += lcmsg_space;
        }

        int passcred_val = 0;
        if (net_socket_cached_int_get(fd, LINUX_SOL_SOCKET, LINUX_SO_PASSCRED,
                                      &passcred_val) &&
            passcred_val) {
            linux_ucred_t cred = {
                .pid = (int32_t) proc_get_pid(),
                .uid = proc_get_uid(),
                .gid = proc_get_gid(),
            };
            uint64_t cred_cmsg_len = 16 + sizeof(cred);
            size_t cred_cmsg_space = (size_t) ((cred_cmsg_len + 7) & ~7ULL);
            if (lpos + cred_cmsg_space <= lctrl_size &&
                lpos + cred_cmsg_space <= lmsg.msg_controllen) {
                int32_t cred_level = 1;
                int32_t cred_type = LINUX_SCM_CREDENTIALS;
                memset(linux_ctrl + lpos, 0, cred_cmsg_space);
                memcpy(linux_ctrl + lpos, &cred_cmsg_len, 8);
                memcpy(linux_ctrl + lpos + 8, &cred_level, 4);
                memcpy(linux_ctrl + lpos + 12, &cred_type, 4);
                memcpy(linux_ctrl + lpos + 16, &cred, sizeof(cred));
                lpos += cred_cmsg_space;
            }
        }

        if (lpos > 0) {
            if (guest_write_small(g, lmsg.msg_control, linux_ctrl, lpos) < 0) {
                recvmsg_cleanup_scm_rights(scm_gfds, scm_hfds, scm_nfds);
                free(lctrl_heap);
                free(mac_ctrl_heap);
                host_fd_ref_close(&host_ref);
                return -LINUX_EFAULT;
            }
            uint64_t lctl = lpos;
            if (guest_write_small(
                    g, msg_gva + offsetof(linux_msghdr_t, msg_controllen),
                    &lctl, sizeof(lctl)) < 0) {
                recvmsg_cleanup_scm_rights(scm_gfds, scm_hfds, scm_nfds);
                free(lctrl_heap);
                free(mac_ctrl_heap);
                host_fd_ref_close(&host_ref);
                return -LINUX_EFAULT;
            }
        } else {
            uint64_t zero64 = 0;
            if (guest_write_small(
                    g, msg_gva + offsetof(linux_msghdr_t, msg_controllen),
                    &zero64, sizeof(zero64)) < 0) {
                free(lctrl_heap);
                free(mac_ctrl_heap);
                host_fd_ref_close(&host_ref);
                return -LINUX_EFAULT;
            }
        }
        free(lctrl_heap);
    } else if (lmsg.msg_control && lmsg.msg_controllen > 0) {
        int injected = 0, passcred_val = 0;
        if (net_socket_cached_int_get(fd, LINUX_SOL_SOCKET, LINUX_SO_PASSCRED,
                                      &passcred_val) &&
            passcred_val) {
            linux_ucred_t cred = {
                .pid = (int32_t) proc_get_pid(),
                .uid = proc_get_uid(),
                .gid = proc_get_gid(),
            };
            uint64_t cred_cmsg_len = 16 + sizeof(cred);
            size_t cred_cmsg_space = (size_t) ((cred_cmsg_len + 7) & ~7ULL);
            if (cred_cmsg_space <= lmsg.msg_controllen &&
                cred_cmsg_space <= 64) {
                uint8_t cred_buf[64];
                int32_t cred_level = 1, cred_type = LINUX_SCM_CREDENTIALS;
                memset(cred_buf, 0, sizeof(cred_buf));
                memcpy(cred_buf, &cred_cmsg_len, 8);
                memcpy(cred_buf + 8, &cred_level, 4);
                memcpy(cred_buf + 12, &cred_type, 4);
                memcpy(cred_buf + 16, &cred, sizeof(cred));
                if (guest_write_small(g, lmsg.msg_control, cred_buf,
                                      cred_cmsg_space) == 0) {
                    uint64_t cred_ctl_len = cred_cmsg_space;
                    guest_write_small(
                        g, msg_gva + offsetof(linux_msghdr_t, msg_controllen),
                        &cred_ctl_len, sizeof(cred_ctl_len));
                    injected = 1;
                }
            }
        }
        if (!injected) {
            uint64_t zero64 = 0;
            guest_write_small(
                g, msg_gva + offsetof(linux_msghdr_t, msg_controllen), &zero64,
                sizeof(zero64));
        }
    } else {
        uint64_t zero64 = 0;
        if (guest_write_small(
                g, msg_gva + offsetof(linux_msghdr_t, msg_controllen), &zero64,
                sizeof(zero64)) < 0) {
            free(mac_ctrl_heap);
            host_fd_ref_close(&host_ref);
            return -LINUX_EFAULT;
        }
    }

    int32_t mflags = mac_to_linux_msg_flags(msg.msg_flags);
    if (ctrl_alloc > 0 && ctrl_truncated)
        mflags |= 0x08;
    if (guest_write_small(g, msg_gva + offsetof(linux_msghdr_t, msg_flags),
                          &mflags, sizeof(mflags)) < 0) {
        recvmsg_cleanup_scm_rights(scm_gfds, scm_hfds, scm_nfds);
        free(mac_ctrl_heap);
        host_fd_ref_close(&host_ref);
        return -LINUX_EFAULT;
    }

    free(mac_ctrl_heap);
    host_fd_ref_close(&host_ref);
    return ret;
}

#define LINUX_MMSGHDR_SIZE 64
#define LINUX_MSG_WAITFORONE 0x10000

static inline int write_linux_mmsghdr_len(guest_t *g,
                                          uint64_t hdr_gva,
                                          uint32_t msg_len)
{
    return guest_write_small(g, hdr_gva + offsetof(linux_mmsghdr_t, msg_len),
                             &msg_len, sizeof(msg_len));
}

int64_t sys_sendmmsg(guest_t *g,
                     int fd,
                     uint64_t mmsg_gva,
                     unsigned int vlen,
                     int flags)
{
    if (vlen == 0)
        return 0;
    if (vlen > 1024)
        vlen = 1024;

    if (vlen == 1) {
        linux_msghdr_t lmsg;
        uint64_t msg_gva = mmsg_gva;
        uint32_t msg_len;
        int mac_flags = translate_msg_flags(flags);
        bool suppress_sigpipe = (flags & 0x4000) != 0;
        host_fd_ref_t host_ref;

        if (host_fd_ref_open(fd, &host_ref) < 0)
            return -LINUX_EBADF;
        if (guest_read_small(g, msg_gva, &lmsg, sizeof(lmsg)) < 0) {
            host_fd_ref_close(&host_ref);
            return -LINUX_EFAULT;
        }

        if ((!lmsg.msg_name || lmsg.msg_namelen == 0) &&
            (!lmsg.msg_control || lmsg.msg_controllen == 0) &&
            lmsg.msg_iovlen == 1) {
            struct {
                uint64_t iov_base, iov_len;
            } guest_iov;
            uint64_t avail = 0;
            void *base = NULL;
            size_t len = 0;

            if (guest_read_small(g, lmsg.msg_iov, &guest_iov,
                                 sizeof(guest_iov)) < 0) {
                host_fd_ref_close(&host_ref);
                return -LINUX_EFAULT;
            }
            if (guest_iov.iov_len > 0) {
                base = guest_ptr_bound(g, guest_iov.iov_base, &avail,
                                       MEM_PERM_R, guest_iov.iov_len);
                if (!base) {
                    host_fd_ref_close(&host_ref);
                    return -LINUX_EFAULT;
                }
                len = guest_iov.iov_len;
                if (len > avail)
                    len = (size_t) avail;
            }

            ssize_t ret = send(host_ref.fd, base, len, mac_flags);
            host_fd_ref_close(&host_ref);
            if (ret < 0) {
                if (errno == EPIPE && !suppress_sigpipe)
                    signal_queue(LINUX_SIGPIPE);
                return linux_errno();
            }
            msg_len = (uint32_t) ret;
            if (write_linux_mmsghdr_len(g, mmsg_gva, msg_len) < 0)
                return -LINUX_EFAULT;
            return 1;
        }

        host_fd_ref_close(&host_ref);
    }

    unsigned int sent = 0;
    for (unsigned int i = 0; i < vlen; i++) {
        uint64_t hdr_gva = mmsg_gva + (uint64_t) i * LINUX_MMSGHDR_SIZE;
        int64_t ret = sys_sendmsg(g, fd, hdr_gva, flags);
        if (ret < 0)
            return sent > 0 ? (int64_t) sent : ret;
        uint32_t msg_len = (uint32_t) ret;
        if (write_linux_mmsghdr_len(g, hdr_gva, msg_len) < 0)
            return sent > 0 ? (int64_t) sent : -LINUX_EFAULT;
        sent++;
    }
    return (int64_t) sent;
}

int64_t sys_recvmmsg(guest_t *g,
                     int fd,
                     uint64_t mmsg_gva,
                     unsigned int vlen,
                     int flags,
                     uint64_t timeout_gva)
{
    if (vlen == 0)
        return 0;
    if (vlen > 1024)
        vlen = 1024;

    if (vlen == 1 && timeout_gva == 0 && !(flags & LINUX_MSG_WAITFORONE)) {
        linux_msghdr_t lmsg;
        uint64_t msg_gva = mmsg_gva;
        int mac_flags = translate_msg_flags(flags);
        host_fd_ref_t host_ref;

        if (host_fd_ref_open(fd, &host_ref) < 0)
            return -LINUX_EBADF;
        if (guest_read_small(g, msg_gva, &lmsg, sizeof(lmsg)) < 0) {
            host_fd_ref_close(&host_ref);
            return -LINUX_EFAULT;
        }

        if (!lmsg.msg_name && (!lmsg.msg_control || lmsg.msg_controllen == 0) &&
            lmsg.msg_iovlen == 1) {
            struct {
                uint64_t iov_base, iov_len;
            } guest_iov;
            uint64_t avail = 0;
            void *base = NULL;
            size_t len = 0;
            uint64_t zero64 = 0;

            if (guest_read_small(g, lmsg.msg_iov, &guest_iov,
                                 sizeof(guest_iov)) < 0) {
                host_fd_ref_close(&host_ref);
                return -LINUX_EFAULT;
            }
            if (guest_iov.iov_len > 0) {
                base = guest_ptr_bound(g, guest_iov.iov_base, &avail,
                                       MEM_PERM_W, guest_iov.iov_len);
                if (!base) {
                    host_fd_ref_close(&host_ref);
                    return -LINUX_EFAULT;
                }
                len = guest_iov.iov_len;
                if (len > avail)
                    len = (size_t) avail;
            }

            struct iovec host_iov = {.iov_base = base, .iov_len = len};
            struct msghdr host_msg = {
                .msg_iov = &host_iov,
                .msg_iovlen = 1,
            };
            ssize_t ret = recvmsg(host_ref.fd, &host_msg, mac_flags);
            if (ret < 0) {
                host_fd_ref_close(&host_ref);
                return linux_errno();
            }
            uint32_t msg_len = (uint32_t) ret;
            int32_t out_flags =
                (int32_t) mac_to_linux_msg_flags(host_msg.msg_flags);
            if (guest_write_small(
                    g, msg_gva + offsetof(linux_msghdr_t, msg_controllen),
                    &zero64, sizeof(zero64)) < 0 ||
                guest_write_small(g,
                                  msg_gva + offsetof(linux_msghdr_t, msg_flags),
                                  &out_flags, sizeof(out_flags)) < 0 ||
                write_linux_mmsghdr_len(g, mmsg_gva, msg_len) < 0) {
                host_fd_ref_close(&host_ref);
                return -LINUX_EFAULT;
            }
            host_fd_ref_close(&host_ref);
            return 1;
        }

        host_fd_ref_close(&host_ref);
    }

    if (timeout_gva) {
        linux_timespec_t ts;
        if (guest_read_small(g, timeout_gva, &ts, sizeof(ts)) == 0) {
            host_fd_ref_t host_ref;
            if (host_fd_ref_open(fd, &host_ref) < 0)
                return -LINUX_EBADF;
            if (ts.tv_sec < 0 || !RANGE_CHECK(ts.tv_nsec, 0, 1000000000LL)) {
                host_fd_ref_close(&host_ref);
                return -LINUX_EINVAL;
            }
            int timeout_ms;
            if (ts.tv_sec > 2000000)
                timeout_ms = -1;
            else
                timeout_ms = (int) (ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
            struct pollfd pfd = {.fd = host_ref.fd, .events = POLLIN};
            int pr = poll(&pfd, 1, timeout_ms);
            host_fd_ref_close(&host_ref);
            if (pr == 0)
                return -LINUX_EAGAIN;
            if (pr < 0)
                return linux_errno();
        }
    }

    bool waitforone = (flags & LINUX_MSG_WAITFORONE) != 0;
    int msg_flags = flags & ~LINUX_MSG_WAITFORONE;

    unsigned int received = 0;
    for (unsigned int i = 0; i < vlen; i++) {
        uint64_t hdr_gva = mmsg_gva + (uint64_t) i * LINUX_MMSGHDR_SIZE;
        int cur_flags = msg_flags;
        if (waitforone && received > 0)
            cur_flags |= 0x40;
        int64_t ret = sys_recvmsg(g, fd, hdr_gva, cur_flags);
        if (ret < 0)
            return received > 0 ? (int64_t) received : ret;
        uint32_t msg_len = (uint32_t) ret;
        if (write_linux_mmsghdr_len(g, hdr_gva, msg_len) < 0)
            return received > 0 ? (int64_t) received : -LINUX_EFAULT;
        received++;
    }
    return (int64_t) received;
}
