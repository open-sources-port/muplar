/* Poll/select/epoll syscall handlers
 *
 * Copyright 2026 elfuse contributors
 * Copyright 2025 Moritz Angermann, zw3rk pte. ltd.
 * SPDX-License-Identifier: Apache-2.0
 *
 * ppoll, pselect6, and epoll (emulated via macOS kqueue).
 */

#pragma once

#include <stdint.h>
#include "core/guest.h"

/* polling/select */
int64_t sys_ppoll(guest_t *g,
                  uint64_t fds_gva,
                  uint32_t nfds,
                  uint64_t timeout_gva,
                  uint64_t sigmask_gva);
int64_t sys_pselect6(guest_t *g,
                     int nfds,
                     uint64_t readfds_gva,
                     uint64_t writefds_gva,
                     uint64_t exceptfds_gva,
                     uint64_t timeout_gva,
                     uint64_t sigmask_gva);

/* epoll (emulated via kqueue) */
int64_t sys_epoll_create1(int flags);
int64_t sys_epoll_ctl(guest_t *g, int epfd, int op, int fd, uint64_t event_gva);
int64_t sys_epoll_pwait(guest_t *g,
                        int epfd,
                        uint64_t events_gva,
                        int maxevents,
                        int timeout_ms,
                        uint64_t sigmask_gva);

/* Global wakeup pipe for interrupting blocking poll/select/epoll.
 * When exit_group or futex_interrupt is requested, write to
 * wakeup_pipe_wr to unblock any thread stuck in a host-side
 * poll/select/kevent with infinite timeout.
 */
void wakeup_pipe_init(void);
void wakeup_pipe_signal(void);
