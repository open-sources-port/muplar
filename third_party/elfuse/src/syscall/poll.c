/* Poll/select/epoll syscall handlers
 *
 * Copyright 2026 elfuse contributors
 * Copyright 2025 Moritz Angermann, zw3rk pte. ltd.
 * SPDX-License-Identifier: Apache-2.0
 *
 * ppoll, pselect6, and epoll (emulated via macOS kqueue). All functions
 * are called from syscall_dispatch() in syscall/syscall.c.
 */

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <limits.h>
#include <sys/event.h>
#include <poll.h>

#include "utils.h"

#include "debug/log.h"

#include "runtime/futex.h"

#include "syscall/abi.h"
#include "syscall/internal.h"
#include "syscall/poll.h"
#include "syscall/proc.h" /* proc_exit_group_requested */
#include "syscall/signal.h"

/* Global wakeup pipe: write end signals exit_group/futex_interrupt to threads
 * blocked in host poll/select/kevent. The read end is added to every blocking
 * wait with infinite timeout.
 */
static int wakeup_pipe_rd = -1, wakeup_pipe_wr = -1;

void wakeup_pipe_init(void)
{
    int pipefd[2];
    if (pipe(pipefd) == 0) {
        fcntl(pipefd[0], F_SETFL, O_NONBLOCK);
        fcntl(pipefd[1], F_SETFL, O_NONBLOCK);
        wakeup_pipe_rd = pipefd[0];
        wakeup_pipe_wr = pipefd[1];
    }
}

void wakeup_pipe_signal(void)
{
    if (wakeup_pipe_wr >= 0) {
        uint8_t byte = 1;
        write(wakeup_pipe_wr, &byte, 1);
    }
}

/* polling/select. */

#define PPOLL_FD_CACHE_SIZE 512

typedef struct {
    int host_fd;
    uint16_t word;
    uint8_t bit_index;
} pselect_req_t;

static int ppoll_lookup_host_fd(const int *cached_guest_fds,
                                const int *cached_host_fds,
                                int guest_fd)
{
    unsigned int slot =
        ((unsigned int) guest_fd * 2654435761u) & (PPOLL_FD_CACHE_SIZE - 1);

    for (int probes = 0; probes < PPOLL_FD_CACHE_SIZE; probes++) {
        if (cached_guest_fds[slot] == -1)
            return -1;
        if (cached_guest_fds[slot] == guest_fd)
            return cached_host_fds[slot];
        slot = (slot + 1) & (PPOLL_FD_CACHE_SIZE - 1);
    }

    return -1;
}

static void ppoll_cache_host_fd(int *cached_guest_fds,
                                int *cached_host_fds,
                                int guest_fd,
                                int host_fd)
{
    unsigned int slot =
        ((unsigned int) guest_fd * 2654435761u) & (PPOLL_FD_CACHE_SIZE - 1);

    for (int probes = 0; probes < PPOLL_FD_CACHE_SIZE; probes++) {
        if (cached_guest_fds[slot] == -1 ||
            cached_guest_fds[slot] == guest_fd) {
            cached_guest_fds[slot] = guest_fd;
            cached_host_fds[slot] = host_fd;
            return;
        }
        slot = (slot + 1) & (PPOLL_FD_CACHE_SIZE - 1);
    }
}

int64_t sys_ppoll(guest_t *g,
                  uint64_t fds_gva,
                  uint32_t nfds,
                  uint64_t timeout_gva,
                  uint64_t sigmask_gva)
{
    if (nfds > 256)
        return -LINUX_EINVAL;

    /* Read pollfd array from guest (layout is identical to macOS) */
    linux_pollfd_t guest_fds[256];
    if (nfds > 0) {
        if (guest_read_small(g, fds_gva, guest_fds,
                             nfds * sizeof(linux_pollfd_t)) < 0)
            return -LINUX_EFAULT;
    }

    /* Translate guest FDs to host FDs */
    struct pollfd host_fds[256];
    int cached_guest_fds[PPOLL_FD_CACHE_SIZE];
    int cached_host_fds[PPOLL_FD_CACHE_SIZE];
    int seen_guest_fds[32], seen_host_fds[32];
    uint32_t seen_count = 0;
    bool use_linear_cache = (nfds <= 32);

    if (!use_linear_cache)
        memset(cached_guest_fds, 0xff, sizeof(cached_guest_fds));

    for (uint32_t i = 0; i < nfds; i++) {
        int guest_fd = guest_fds[i].fd, host_fd = -1;
        if (use_linear_cache) {
            for (uint32_t j = 0; j < seen_count; j++) {
                if (seen_guest_fds[j] == guest_fd) {
                    host_fd = seen_host_fds[j];
                    break;
                }
            }
            if (host_fd < 0) {
                host_fd = fd_to_host(guest_fd);
                seen_guest_fds[seen_count] = guest_fd;
                seen_host_fds[seen_count] = host_fd;
                seen_count++;
            }
        } else {
            host_fd = ppoll_lookup_host_fd(cached_guest_fds, cached_host_fds,
                                           guest_fd);
            if (host_fd < 0) {
                host_fd = fd_to_host(guest_fd);
                ppoll_cache_host_fd(cached_guest_fds, cached_host_fds, guest_fd,
                                    host_fd);
            }
        }
        host_fds[i].fd = host_fd;
        host_fds[i].events = guest_fds[i].events;
        host_fds[i].revents = 0;
    }

    /* Log fd types for shutdown diagnostics (verbose only) */
    if (timeout_gva == 0) {
        char fdbuf[256];
        int pos = 0;
        for (uint32_t i = 0; i < nfds && i < 8; i++) {
            int gfd = guest_fds[i].fd;
            const char *type = "?";
            if (RANGE_CHECK(gfd, 0, FD_TABLE_SIZE)) {
                switch (fd_table[gfd].type) {
                case FD_EVENTFD:
                    type = "efd";
                    break;
                case FD_TIMERFD:
                    type = "tfd";
                    break;
                case FD_EPOLL:
                    type = "epoll";
                    break;
                case FD_SIGNALFD:
                    type = "sfd";
                    break;
                default:
                    type = "fd";
                    break;
                }
            }
            pos += snprintf(fdbuf + pos, sizeof(fdbuf) - (size_t) pos,
                            "%s%d(%s->%d)", i ? "," : "", gfd, type,
                            host_fds[i].fd);
        }
        log_debug("ppoll: nfds=%u infinite timeout, fds=[%s]", nfds, fdbuf);
    }

    /* Convert timeout (compute in int64_t to avoid overflow, clamp to INT_MAX)
     */
    int timeout_ms = -1; /* Infinite by default */
    if (timeout_gva != 0) {
        linux_timespec_t lts;
        if (guest_read_small(g, timeout_gva, &lts, sizeof(lts)) < 0)
            return -LINUX_EFAULT;
        /* Linux returns EINVAL for negative timeout values */
        if (lts.tv_sec < 0 || !RANGE_CHECK(lts.tv_nsec, 0, 1000000000LL))
            return -LINUX_EINVAL;
        /* Guard against overflow: tv_sec * 1000 can exceed INT64_MAX */
        int64_t ms64;
        if (lts.tv_sec > INT64_MAX / 1000)
            ms64 = INT64_MAX;
        else
            ms64 = lts.tv_sec * (int64_t) 1000 + lts.tv_nsec / 1000000;
        timeout_ms = (ms64 > INT_MAX) ? INT_MAX : (int) ms64;
    }

    /* Atomically install signal mask for the duration of the poll */
    uint64_t saved_mask = 0;
    bool mask_installed = false;
    if (sigmask_gva != 0) {
        uint64_t new_mask;
        if (guest_read_small(g, sigmask_gva, &new_mask, sizeof(new_mask)) ==
            0) {
            saved_mask = signal_save_blocked();
            signal_set_blocked(new_mask);
            mask_installed = true;
        }
    }

    /* For indefinite polls, add the wakeup pipe so exit_group can
     * interrupt threads blocked in host poll(). Without this, threads
     * in poll(timeout=-1) cannot be interrupted by hv_vcpus_exit()
     * because they're not in hv_vcpu_run().
     */
    bool added_wakeup = false;
    if (timeout_ms < 0 && wakeup_pipe_rd >= 0 && nfds < 256) {
        host_fds[nfds].fd = wakeup_pipe_rd;
        host_fds[nfds].events = POLLIN;
        host_fds[nfds].revents = 0;
        added_wakeup = true;
    }

    int ret;
    do {
        ret = poll(host_fds, nfds + added_wakeup,
                   timeout_ms < 0 ? 200 : timeout_ms);

        /* Check for exit_group / futex_interrupt after waking */
        if (proc_exit_group_requested() || futex_interrupt_pending()) {
            ret = -1;
            errno = EINTR;
            break;
        }

        /* If poll emulation used a short timeout (200ms) on an infinite poll
         * and nothing happened, loop back. If the caller had a real timeout,
         * poll emulation only called poll once with that timeout, so break.
         */
    } while (ret == 0 && timeout_ms < 0);

    int saved_errno = errno;

    /* Drain the wakeup pipe if it fired, and subtract from count since the
     * wakeup pipe is not visible to the guest.
     */
    if (added_wakeup && (host_fds[nfds].revents & POLLIN)) {
        uint8_t drain;
        while (read(wakeup_pipe_rd, &drain, 1) > 0)
            ;
        if (ret > 0)
            ret--;
    }

    /* Restore original signal mask */
    if (mask_installed)
        signal_restore_blocked(saved_mask);

    if (ret < 0) {
        errno = saved_errno;
        return linux_errno();
    }

    /* Write back revents to guest only when the guest-visible array changes.
     * Tight ppoll(..., timeout=0) loops often come back with all-zero revents,
     * in which case rewriting the whole pollfd array is wasted work.
     */
    bool changed = false;
    for (uint32_t i = 0; i < nfds; i++) {
        if (guest_fds[i].revents != host_fds[i].revents)
            changed = true;
        guest_fds[i].revents = host_fds[i].revents;
    }
    if (changed && nfds > 0) {
        if (guest_write_small(g, fds_gva, guest_fds,
                              nfds * sizeof(linux_pollfd_t)) < 0)
            return -LINUX_EFAULT;
    }

    return ret;
}

int64_t sys_pselect6(guest_t *g,
                     int nfds,
                     uint64_t readfds_gva,
                     uint64_t writefds_gva,
                     uint64_t exceptfds_gva,
                     uint64_t timeout_gva,
                     uint64_t sigmask_gva)
{
    /* pselect6 atomically sets the signal mask during the wait, then
     * restores it. The sixth argument is a pointer to a struct:
     *   { const sigset_t *ss; size_t ss_len; }
     */
    if (nfds < 0 || nfds > FD_SETSIZE)
        return -LINUX_EINVAL;

    if (nfds == 0 && readfds_gva == 0 && writefds_gva == 0 &&
        exceptfds_gva == 0 && sigmask_gva == 0 && timeout_gva != 0) {
        linux_timespec_t lts;
        if (guest_read_small(g, timeout_gva, &lts, sizeof(lts)) < 0)
            return -LINUX_EFAULT;
        if (lts.tv_sec < 0 || !RANGE_CHECK(lts.tv_nsec, 0, 1000000000LL))
            return -LINUX_EINVAL;
        if (lts.tv_sec == 0 && lts.tv_nsec == 0)
            return 0;
    }

    fd_set read_set, write_set, except_set;
    fd_set *read_setp = NULL;
    fd_set *write_setp = NULL;
    fd_set *except_setp = NULL;
    FD_ZERO(&read_set);
    if (writefds_gva)
        FD_ZERO(&write_set);
    if (exceptfds_gva)
        FD_ZERO(&except_set);

    if (readfds_gva)
        read_setp = &read_set;
    if (writefds_gva)
        write_setp = &write_set;
    if (exceptfds_gva)
        except_setp = &except_set;

    int max_host_fd = -1, nfds_words = (nfds + 63) / 64;
    pselect_req_t reqs_stack[64];
    pselect_req_t *reqs = reqs_stack;
    pselect_req_t *reqs_heap = NULL;
    int req_count = 0;

    /* Translate fd_sets from guest. Linux fd_set uses unsigned long bitmask.
     * FD_TABLE_SIZE=1024 -> max 16 uint64_t words (128 bytes).
     */
    if (readfds_gva || writefds_gva || exceptfds_gva) {
        uint64_t rbits_buf[FD_TABLE_SIZE / 64], wbits_buf[FD_TABLE_SIZE / 64];
        uint64_t ebits_buf[FD_TABLE_SIZE / 64];
        uint64_t *rbits = NULL;
        uint64_t *wbits = NULL;
        uint64_t *ebits = NULL;
        int req_cap = 0;
        size_t bitmask_bytes = (size_t) nfds_words * 8;
        if (readfds_gva) {
            memset(rbits_buf, 0, sizeof(rbits_buf));
            rbits = rbits_buf;
        }
        if (writefds_gva) {
            memset(wbits_buf, 0, sizeof(wbits_buf));
            wbits = wbits_buf;
        }
        if (exceptfds_gva) {
            memset(ebits_buf, 0, sizeof(ebits_buf));
            ebits = ebits_buf;
        }
        if (readfds_gva &&
            guest_read_small(g, readfds_gva, rbits, bitmask_bytes) < 0)
            return -LINUX_EFAULT;
        if (writefds_gva &&
            guest_read_small(g, writefds_gva, wbits, bitmask_bytes) < 0)
            return -LINUX_EFAULT;
        if (exceptfds_gva &&
            guest_read_small(g, exceptfds_gva, ebits, bitmask_bytes) < 0)
            return -LINUX_EFAULT;

        for (int word = 0; word < nfds_words; word++) {
            uint64_t requested = (rbits ? rbits[word] : 0) |
                                 (wbits ? wbits[word] : 0) |
                                 (ebits ? ebits[word] : 0);
            req_cap += bit_popcount64(requested);
        }
        if (req_cap > (int) (ARRAY_SIZE(reqs_stack))) {
            reqs_heap = malloc((size_t) req_cap * sizeof(*reqs_heap));
            if (!reqs_heap)
                return -LINUX_ENOMEM;
            reqs = reqs_heap;
        }

        for (int word = 0; word < nfds_words; word++) {
            uint64_t requested = (rbits ? rbits[word] : 0) |
                                 (wbits ? wbits[word] : 0) |
                                 (ebits ? ebits[word] : 0);
            while (requested) {
                int bit_index = bit_ctz64(requested);
                int i = word * 64 + bit_index;
                uint64_t bit = BIT64(bit_index);
                int host_fd = fd_to_host(i);
                reqs[req_count].host_fd = host_fd;
                reqs[req_count].word = (uint16_t) word;
                reqs[req_count].bit_index = (uint8_t) bit_index;
                req_count++;
                if (RANGE_CHECK(host_fd, 0, FD_SETSIZE)) {
                    if (host_fd > max_host_fd)
                        max_host_fd = host_fd;
                    if (rbits && (rbits[word] & bit))
                        FD_SET(host_fd, read_setp);
                    if (wbits && (wbits[word] & bit))
                        FD_SET(host_fd, write_setp);
                    if (ebits && (ebits[word] & bit))
                        FD_SET(host_fd, except_setp);
                }
                requested &= requested - 1;
            }
        }
    }

    bool has_timeout = (timeout_gva != 0);
    struct timespec ts;
    if (has_timeout) {
        linux_timespec_t lts;
        if (guest_read_small(g, timeout_gva, &lts, sizeof(lts)) < 0)
            goto pselect_fault;
        /* Linux returns EINVAL for negative or out-of-range timeout values */
        if (lts.tv_sec < 0 || !RANGE_CHECK(lts.tv_nsec, 0, 1000000000LL))
            goto pselect_inval;
        ts.tv_sec = lts.tv_sec;
        ts.tv_nsec = lts.tv_nsec;
    }

    /* Apply signal mask atomically around the select.
     * Linux pselect6 arg6 points to { sigset_t *ss; size_t ss_len }.
     * Save the current blocked mask, apply the new one, do the select, then
     * restore the original mask.
     */
    uint64_t saved_blocked = 0;
    bool mask_applied = false;
    if (sigmask_gva) {
        struct {
            uint64_t ss, ss_len;
        } ssarg;
        if (guest_read_small(g, sigmask_gva, &ssarg, sizeof(ssarg)) == 0 &&
            ssarg.ss != 0 && ssarg.ss_len == 8) {
            uint64_t new_mask;
            if (guest_read_small(g, ssarg.ss, &new_mask, sizeof(new_mask)) ==
                0) {
                saved_blocked = signal_save_blocked();
                signal_set_blocked(new_mask);
                mask_applied = true;
            }
        }
    }

    /* For indefinite selects, add the wakeup pipe and use a short timeout so
     * exit_group can interrupt.
     */
    bool added_wakeup = false;
    if (!has_timeout && wakeup_pipe_rd >= 0) {
        FD_SET(wakeup_pipe_rd, &read_set);
        if (wakeup_pipe_rd > max_host_fd)
            max_host_fd = wakeup_pipe_rd;
        added_wakeup = true;
        read_setp = &read_set;
    }

    struct timespec poll_ts = {.tv_sec = 0, .tv_nsec = 200000000L}; /* 200ms */

    /* Save fd_sets because pselect modifies them in-place to indicate ready
     * fds. Without saving/restoring, the indefinite retry loop would operate on
     * corrupted (zeroed) fd_sets after a 200ms timeout iteration.
     */
    fd_set saved_read, saved_write, saved_except;
    if (!has_timeout) {
        if (read_setp)
            saved_read = read_set;
        if (write_setp)
            saved_write = write_set;
        if (except_setp)
            saved_except = except_set;
    }

    int ret;
    do {
        if (!has_timeout) {
            if (read_setp)
                read_set = saved_read;
            if (write_setp)
                write_set = saved_write;
            if (except_setp)
                except_set = saved_except;
        }

        ret = pselect(max_host_fd + 1, read_setp, write_setp, except_setp,
                      has_timeout ? &ts : &poll_ts, NULL);

        if (proc_exit_group_requested() || futex_interrupt_pending()) {
            ret = -1;
            errno = EINTR;
            break;
        }
    } while (ret == 0 && !has_timeout);

    int save_errno = errno;

    /* Drain wakeup pipe if it fired, and subtract from count since the wakeup
     * pipe is not visible to the guest.
     */
    if (added_wakeup && FD_ISSET(wakeup_pipe_rd, &read_set)) {
        uint8_t drain;
        while (read(wakeup_pipe_rd, &drain, 1) > 0)
            ;
        FD_CLR(wakeup_pipe_rd, &read_set);
        if (ret > 0)
            ret--;
    }

    /* Restore original signal mask */
    if (mask_applied)
        signal_restore_blocked(saved_blocked);

    if (ret < 0) {
        errno = save_errno;
        free(reqs_heap);
        return linux_errno();
    }

    /* Write back result fd_sets (zero then set bits for matching fds) */
    if (readfds_gva || writefds_gva || exceptfds_gva) {
        uint64_t rbits_buf[FD_TABLE_SIZE / 64], wbits_buf[FD_TABLE_SIZE / 64];
        uint64_t ebits_buf[FD_TABLE_SIZE / 64];
        uint64_t *rbits = NULL;
        uint64_t *wbits = NULL;
        uint64_t *ebits = NULL;
        if (readfds_gva) {
            memset(rbits_buf, 0, sizeof(rbits_buf));
            rbits = rbits_buf;
        }
        if (writefds_gva) {
            memset(wbits_buf, 0, sizeof(wbits_buf));
            wbits = wbits_buf;
        }
        if (exceptfds_gva) {
            memset(ebits_buf, 0, sizeof(ebits_buf));
            ebits = ebits_buf;
        }
        for (int i = 0; i < req_count; i++) {
            int host_fd = reqs[i].host_fd, word = reqs[i].word;
            uint64_t bit = BIT64(reqs[i].bit_index);
            if (RANGE_CHECK(host_fd, 0, FD_SETSIZE)) {
                if (rbits && FD_ISSET(host_fd, &read_set))
                    rbits[word] |= bit;
                if (wbits && FD_ISSET(host_fd, write_setp))
                    wbits[word] |= bit;
                if (ebits && FD_ISSET(host_fd, except_setp))
                    ebits[word] |= bit;
            }
        }
        int bytes = nfds_words * 8;
        if (rbits && guest_write_small(g, readfds_gva, rbits, bytes) < 0)
            goto pselect_fault;
        if (wbits && guest_write_small(g, writefds_gva, wbits, bytes) < 0)
            goto pselect_fault;
        if (ebits && guest_write_small(g, exceptfds_gva, ebits, bytes) < 0)
            goto pselect_fault;
    }

    free(reqs_heap);
    return ret;

pselect_fault:
    free(reqs_heap);
    return -LINUX_EFAULT;

pselect_inval:
    free(reqs_heap);
    return -LINUX_EINVAL;
}

/* epoll emulation via kqueue
 *
 * Linux epoll is emulated using macOS kqueue. Each epoll_create1() creates
 * a kqueue fd. epoll_ctl translates to kevent() calls. epoll_pwait translates
 * to kevent() with timeout.
 *
 * Limitations:
 *   - EPOLLEXCLUSIVE not supported (rare, for load balancing)
 *   - epoll_data is stored in a per-process registration table indexed by guest
 *     fd (one epoll registration per fd)
 */

/* Linux EPOLL constants */
#define LINUX_EPOLLIN 0x001
#define LINUX_EPOLLOUT 0x004
#define LINUX_EPOLLERR 0x008
#define LINUX_EPOLLHUP 0x010
#define LINUX_EPOLLRDHUP 0x2000
#define LINUX_EPOLLET (1U << 31)
#define LINUX_EPOLLONESHOT (1U << 30)

/* Linux epoll_ctl operations */
#define LINUX_EPOLL_CTL_ADD 1
#define LINUX_EPOLL_CTL_DEL 2
#define LINUX_EPOLL_CTL_MOD 3

/* Linux EPOLL_CLOEXEC = O_CLOEXEC = 0x80000 on aarch64 */
#define LINUX_EPOLL_CLOEXEC 0x80000

/* Linux epoll_event on aarch64 (NOT packed; 16 bytes with padding) */
typedef struct {
    uint32_t events, _pad;
    uint64_t data;
} linux_epoll_event_t;

/* Per-fd registration entry within an epoll instance. */
typedef struct {
    uint32_t events;    /* Registered EPOLL* events mask */
    uint64_t data;      /* User data to return in epoll_wait */
    bool active;        /* Registered in this instance */
    bool oneshot_armed; /* EPOLLONESHOT and event already fired,
                         * waiting for EPOLL_CTL_MOD re-arm.
                         * kqueue removed the event, so poll emulation prevents
                         * reporting but allow MOD.
                         */
} epoll_reg_t;

/* Per-epoll-instance data, stored in fd_table[epfd].dir. Each instance
 * has its own registration table so multiple epoll instances watching
 * the same FD do not overwrite each other's user data.
 */
typedef struct {
    epoll_reg_t regs[FD_TABLE_SIZE];
} epoll_instance_t;

static inline void epoll_merge_event(linux_epoll_event_t *out,
                                     const struct kevent *kev,
                                     const epoll_reg_t *reg)
{
    if (kev->filter == EVFILT_READ)
        out->events |= LINUX_EPOLLIN;
    if (kev->filter == EVFILT_WRITE)
        out->events |= LINUX_EPOLLOUT;
    if (kev->flags & EV_EOF) {
        out->events |= LINUX_EPOLLHUP;
        if (kev->filter == EVFILT_READ && (reg->events & LINUX_EPOLLRDHUP))
            out->events |= LINUX_EPOLLRDHUP;
    }
    if (kev->flags & EV_ERROR)
        out->events |= LINUX_EPOLLERR;
}

int64_t sys_epoll_create1(int flags)
{
    int kq = kqueue();
    if (kq < 0)
        return linux_errno();

    if ((flags & LINUX_EPOLL_CLOEXEC) && fd_set_cloexec(kq) < 0) {
        close(kq);
        return linux_errno();
    }

    /* Allocate per-instance registration table */
    epoll_instance_t *inst = calloc(1, sizeof(epoll_instance_t));
    if (!inst) {
        close(kq);
        return -LINUX_ENOMEM;
    }

    int gfd = fd_alloc(FD_EPOLL, kq, NULL);
    if (gfd < 0) {
        free(inst);
        close(kq);
        return -LINUX_EMFILE;
    }

    fd_table[gfd].dir = inst;
    int lflags = 0;
    if (flags & LINUX_EPOLL_CLOEXEC)
        lflags |= LINUX_O_CLOEXEC;
    fd_table[gfd].linux_flags = lflags;

    return gfd;
}

int64_t sys_epoll_ctl(guest_t *g, int epfd, int op, int fd, uint64_t event_gva)
{
    /* Linux returns EINVAL when trying to add an epoll fd to itself */
    if (fd == epfd)
        return -LINUX_EINVAL;

    host_fd_ref_t epoll_ref;
    if (host_fd_ref_open(epfd, &epoll_ref) < 0)
        return -LINUX_EBADF;
    if (fd_table[epfd].type != FD_EPOLL) {
        host_fd_ref_close(&epoll_ref);
        return -LINUX_EINVAL;
    }

    epoll_instance_t *inst = (epoll_instance_t *) fd_table[epfd].dir;
    if (!inst) {
        host_fd_ref_close(&epoll_ref);
        return -LINUX_EINVAL;
    }

    host_fd_ref_t target_ref;
    if (host_fd_ref_open(fd, &target_ref) < 0) {
        host_fd_ref_close(&epoll_ref);
        return -LINUX_EBADF;
    }

    epoll_reg_t *reg = &inst->regs[fd];

    if (op == LINUX_EPOLL_CTL_DEL) {
        /* Linux returns ENOENT when removing an unregistered fd */
        if (!reg->active) {
            host_fd_ref_close(&target_ref);
            host_fd_ref_close(&epoll_ref);
            return -LINUX_ENOENT;
        }

        /* Remove all filters for this fd. EPOLLRDHUP alone registers
         * EVFILT_READ (see ADD path), so check both EPOLLIN and EPOLLRDHUP.
         */
        struct kevent changes[2];
        int nchanges = 0;
        {
            if (reg->events & (LINUX_EPOLLIN | LINUX_EPOLLRDHUP)) {
                EV_SET(&changes[nchanges], target_ref.fd, EVFILT_READ,
                       EV_DELETE, 0, 0, NULL);
                nchanges++;
            }
            if (reg->events & LINUX_EPOLLOUT) {
                EV_SET(&changes[nchanges], target_ref.fd, EVFILT_WRITE,
                       EV_DELETE, 0, 0, NULL);
                nchanges++;
            }
            /* Ignore errors from EV_DELETE (fd might already be closed) */
            kevent(epoll_ref.fd, changes, nchanges, NULL, 0, NULL);
            reg->active = false;
            /* Clear stale state for potential re-add */
            reg->oneshot_armed = false;
        }
        host_fd_ref_close(&target_ref);
        host_fd_ref_close(&epoll_ref);
        return 0;
    }

    /* Linux semantics: ADD fails with EEXIST if already registered; MOD fails
     * with ENOENT if not registered. oneshot_armed registrations
     * (EPOLLONESHOT fired, waiting for re-arm) are still valid for MOD.
     */
    if (op == LINUX_EPOLL_CTL_ADD && reg->active) {
        host_fd_ref_close(&target_ref);
        host_fd_ref_close(&epoll_ref);
        return -LINUX_EEXIST;
    }
    if (op == LINUX_EPOLL_CTL_MOD && !reg->active && !reg->oneshot_armed) {
        host_fd_ref_close(&target_ref);
        host_fd_ref_close(&epoll_ref);
        return -LINUX_ENOENT;
    }

    /* ADD or MOD: read the epoll_event from guest */
    linux_epoll_event_t ev;
    if (guest_read_small(g, event_gva, &ev, sizeof(ev)) < 0) {
        host_fd_ref_close(&target_ref);
        host_fd_ref_close(&epoll_ref);
        return -LINUX_EFAULT;
    }

    /* For MOD, remove old registrations first if they exist in kqueue.
     * EPOLLRDHUP alone registers EVFILT_READ (see ADD path), so check both
     * EPOLLIN and EPOLLRDHUP (same logic as CTL_DEL). Always attempt the
     * deletes even when oneshot_armed: with multi-filter EPOLLONESHOT, only
     * the filter that fired was removed by EV_ONESHOT; the other filter is
     * still registered and must be cleaned. Issue each delete in its own
     * kevent call so an ENOENT on one filter does not abort the other —
     * with a single batched call and NULL eventlist, kevent stops at the
     * first failed change and leaks the survivor.
     */
    if (op == LINUX_EPOLL_CTL_MOD && reg->active) {
        struct kevent del;
        if (reg->events & (LINUX_EPOLLIN | LINUX_EPOLLRDHUP)) {
            EV_SET(&del, target_ref.fd, EVFILT_READ, EV_DELETE, 0, 0, NULL);
            kevent(epoll_ref.fd, &del, 1, NULL, 0, NULL);
        }
        if (reg->events & LINUX_EPOLLOUT) {
            EV_SET(&del, target_ref.fd, EVFILT_WRITE, EV_DELETE, 0, 0, NULL);
            kevent(epoll_ref.fd, &del, 1, NULL, 0, NULL);
        }
    }

    /* Build kevent changes */
    struct kevent changes[2];
    int nchanges = 0;

    /* EPOLLET maps to EV_CLEAR. Idle re-poll, full drain, and post-drain
     * new-data edges all match Linux EPOLLET. Narrow divergence: a partial read
     * (without draining to EAGAIN) is a data-count state change that re-arms
     * kqueue, so the next kevent fires while Linux EPOLLET stays silent.
     * Distinguishing "partial-read remainder" from "drained-then-refilled"
     * would require a unified drain signal across every data-consuming path
     * (read / recv* / splice / ...) feeding back into this layer, which the
     * epoll/kqueue bridge does not maintain. Apps that follow the documented
     * EPOLLET contract (drain to EAGAIN) are unaffected; tests/test-epoll-
     * edge.c locks in that contract.
     */
    uint16_t kflags = EV_ADD;
    if (ev.events & LINUX_EPOLLET)
        kflags |= EV_CLEAR;
    if (ev.events & LINUX_EPOLLONESHOT)
        kflags |= EV_ONESHOT;

    /* Use (void*)(uintptr_t)fd as udata to identify the guest fd */
    void *udata = (void *) (uintptr_t) fd;

    if (ev.events & (LINUX_EPOLLIN | LINUX_EPOLLRDHUP)) {
        EV_SET(&changes[nchanges], target_ref.fd, EVFILT_READ, kflags, 0, 0,
               udata);
        nchanges++;
    }
    if (ev.events & LINUX_EPOLLOUT) {
        EV_SET(&changes[nchanges], target_ref.fd, EVFILT_WRITE, kflags, 0, 0,
               udata);
        nchanges++;
    }

    if (nchanges > 0) {
        if (kevent(epoll_ref.fd, changes, nchanges, NULL, 0, NULL) < 0) {
            host_fd_ref_close(&target_ref);
            host_fd_ref_close(&epoll_ref);
            return linux_errno();
        }
    }

    /* Store registration data in per-instance table.
     * Clear oneshot_armed when MOD successfully re-arms.
     */
    reg->events = ev.events;
    reg->data = ev.data;
    reg->active = true;
    reg->oneshot_armed = false;

    host_fd_ref_close(&target_ref);
    host_fd_ref_close(&epoll_ref);
    return 0;
}

int64_t sys_epoll_pwait(guest_t *g,
                        int epfd,
                        uint64_t events_gva,
                        int maxevents,
                        int timeout_ms,
                        uint64_t sigmask_gva)
{
    host_fd_ref_t epoll_ref;
    if (host_fd_ref_open(epfd, &epoll_ref) < 0)
        return -LINUX_EBADF;
    if (fd_table[epfd].type != FD_EPOLL) {
        host_fd_ref_close(&epoll_ref);
        return -LINUX_EINVAL;
    }
    if (maxevents <= 0) {
        host_fd_ref_close(&epoll_ref);
        return -LINUX_EINVAL;
    }

    epoll_instance_t *inst = (epoll_instance_t *) fd_table[epfd].dir;
    if (!inst) {
        host_fd_ref_close(&epoll_ref);
        return -LINUX_EINVAL;
    }

    /* Atomically install signal mask for the duration of the wait */
    uint64_t saved_mask = 0;
    bool mask_installed = false;
    if (sigmask_gva != 0) {
        uint64_t new_mask;
        if (guest_read_small(g, sigmask_gva, &new_mask, sizeof(new_mask)) ==
            0) {
            saved_mask = signal_save_blocked();
            signal_set_blocked(new_mask);
            mask_installed = true;
        }
    }

    /* Convert timeout */
    bool has_timeout = (timeout_ms >= 0);
    struct timespec ts;
    if (has_timeout) {
        ts.tv_sec = timeout_ms / 1000;
        ts.tv_nsec = (timeout_ms % 1000) * 1000000L;
    }

    /* For indefinite waits, register the wakeup pipe with the kqueue
     * so exit_group can interrupt threads blocked in kevent().
     */
    bool added_wakeup = false;
    if (!has_timeout && wakeup_pipe_rd >= 0) {
        struct kevent wake_ev;
        EV_SET(&wake_ev, wakeup_pipe_rd, EVFILT_READ, EV_ADD | EV_ONESHOT, 0, 0,
               (void *) (uintptr_t) -1);
        kevent(epoll_ref.fd, &wake_ev, 1, NULL, 0, NULL);
        added_wakeup = true;
    }

    /* Collect kqueue events. For indefinite waits, use a short timeout and loop
     * so exit_group can interrupt. Cap maxevents before multiply to avoid
     * signed integer overflow when maxevents is very large.
     */
    if (maxevents > 128)
        maxevents = 128;
    int cap = maxevents * 2; /* Each epoll fd can produce 2 kevents */
    if (cap > 256)
        cap = 256;
    /* Reserve one slot for the wakeup pipe event */
    if (added_wakeup && cap < 256)
        cap++;
    struct kevent kevents[256];

    struct timespec poll_ts = {.tv_sec = 0, .tv_nsec = 200000000L}; /* 200ms */
    int nready;
    do {
        nready = kevent(epoll_ref.fd, NULL, 0, kevents, cap,
                        has_timeout ? &ts : &poll_ts);

        if (proc_exit_group_requested() || futex_interrupt_pending()) {
            nready = -1;
            errno = EINTR;
            break;
        }
    } while (nready == 0 && !has_timeout);

    int saved_errno = errno;

    /* Remove wakeup pipe registration and drain if it fired */
    if (added_wakeup) {
        struct kevent del_ev;
        EV_SET(&del_ev, wakeup_pipe_rd, EVFILT_READ, EV_DELETE, 0, 0, NULL);
        kevent(epoll_ref.fd, &del_ev, 1, NULL, 0, NULL);
        /* Drain the wakeup pipe */
        uint8_t drain;
        while (read(wakeup_pipe_rd, &drain, 1) > 0)
            ;
        /* Filter out wakeup pipe events from results */
        for (int i = 0; i < nready; i++) {
            if ((uintptr_t) kevents[i].udata == (uintptr_t) -1) {
                kevents[i] = kevents[nready - 1];
                nready--;
                i--;
            }
        }
    }

    /* Restore original signal mask after the blocking wait */
    if (mask_installed)
        signal_restore_blocked(saved_mask);

    if (nready < 0) {
        errno = saved_errno;
        host_fd_ref_close(&epoll_ref);
        return linux_errno();
    }

    /* Merge kevent results into epoll_event results. Multiple kevents for the
     * same fd (READ + WRITE) merge into one epoll_event.
     * Use guest FD (not user data) as the merge key, since two different FDs
     * could legitimately share the same epoll_data value.
     */
    linux_epoll_event_t out[256];
    /* Parallel array tracking which guest FD each output entry represents. */
    uint16_t out_gfds[256];
    int16_t out_index[FD_TABLE_SIZE];
    int nout = 0;

    memset(out_index, 0xff, sizeof(out_index));

    for (int i = 0; i < nready && nout < maxevents; i++) {
        int gfd = (int) (uintptr_t) kevents[i].udata;
        if (!RANGE_CHECK(gfd, 0, FD_TABLE_SIZE) || !inst->regs[gfd].active)
            continue;

        /* EPOLLONESHOT semantics: once any event fired and was reported, the
         * fd stays disarmed until EPOLL_CTL_MOD re-arms it. With multi-filter
         * registrations (e.g. EPOLLIN | EPOLLOUT), EV_ONESHOT only removed the
         * filter that fired; surviving filters can still fire later and would
         * be reported here without this guard.
         */
        if (inst->regs[gfd].oneshot_armed)
            continue;

        epoll_reg_t *reg = &inst->regs[gfd];

        int idx = out_index[gfd];
        if (idx >= 0) {
            epoll_merge_event(&out[idx], &kevents[i], reg);
            continue;
        }

        idx = nout++;
        out_index[gfd] = idx;
        out_gfds[idx] = gfd;
        out[idx].events = 0;
        out[idx]._pad = 0;
        out[idx].data = reg->data;
        epoll_merge_event(&out[idx], &kevents[i], reg);
    }

    /* Mark EPOLLONESHOT FDs as armed (fired but waiting for MOD re-arm).
     * kqueue already removed the event (EV_ONESHOT), so poll emulation marks
     * the registration as oneshot_armed to allow MOD but prevent further event
     * reporting until re-armed.
     */
    for (int i = 0; i < nout; i++) {
        int gfd = out_gfds[i];
        if (RANGE_CHECK(gfd, 0, FD_TABLE_SIZE) && inst->regs[gfd].active) {
            if (inst->regs[gfd].events & LINUX_EPOLLONESHOT)
                inst->regs[gfd].oneshot_armed = true;
        }
    }

    /* Write results to guest */
    if (nout > 0) {
        if (guest_write_small(g, events_gva, out,
                              nout * sizeof(linux_epoll_event_t)) < 0) {
            host_fd_ref_close(&epoll_ref);
            return -LINUX_EFAULT;
        }
    }

    host_fd_ref_close(&epoll_ref);
    return nout;
}
