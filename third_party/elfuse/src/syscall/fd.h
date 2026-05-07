/* Special FD types (eventfd, signalfd, timerfd)
 *
 * Copyright 2026 elfuse contributors
 * Copyright 2025 Moritz Angermann, zw3rk pte. ltd.
 * SPDX-License-Identifier: Apache-2.0
 *
 * Emulates Linux eventfd (pipe+counter), signalfd (synthetic signal reads), and
 * timerfd (kqueue EVFILT_TIMER). Each provides special read/write/close
 * semantics dispatched from sys_read/sys_write/sys_close.
 */

#pragma once

#include <stdint.h>
#include "core/guest.h"

/* Initialize all special FD subsystem state arrays. Must be called
 * once from syscall_init() before any guest code runs.
 */
void timerfd_init(void);
void eventfd_init(void);
void signalfd_init(void);

/* timerfd (emulated via kqueue) */
int64_t sys_timerfd_create(int clockid, int flags);
int64_t sys_timerfd_settime(guest_t *g,
                            int fd,
                            int flags,
                            uint64_t new_value_gva,
                            uint64_t old_value_gva);
int64_t sys_timerfd_gettime(guest_t *g, int fd, uint64_t curr_value_gva);

/* eventfd (emulated via pipe + counter) */
int64_t sys_eventfd2(unsigned int initval, int flags);

/* signalfd (emulated via synthetic signal reads) */
int64_t sys_signalfd4(guest_t *g,
                      int fd,
                      uint64_t mask_gva,
                      uint64_t sigsetsize,
                      int flags);

/* Special read/write handlers for eventfd, signalfd, and timerfd FD types.
 * Called from sys_read/sys_write when the fd type requires special semantics
 * (8-byte counter for eventfd, signalfd_siginfo for signalfd, 8-byte
 * expiration count for timerfd).
 */
int64_t eventfd_read(int guest_fd,
                     guest_t *g,
                     uint64_t buf_gva,
                     uint64_t count);
int64_t eventfd_write(int guest_fd,
                      guest_t *g,
                      uint64_t buf_gva,
                      uint64_t count);
int64_t signalfd_read(int guest_fd,
                      guest_t *g,
                      uint64_t buf_gva,
                      uint64_t count);
int64_t timerfd_read(int guest_fd,
                     guest_t *g,
                     uint64_t buf_gva,
                     uint64_t count);

/* Notify signalfd pipes when a signal is queued. Called from signal_queue();
 * writes a byte to make poll/epoll see readability.
 */
void signalfd_notify(int signum);

/* Snapshot per-fd state for /proc/self/fdinfo. Each accessor returns true when
 * the guest_fd refers to a live instance of that special-fd type. The values
 * are read under sfd_lock so concurrent read/write/settime cannot tear them.
 */
bool eventfd_fdinfo_snapshot(int guest_fd, uint64_t *count_out);
bool signalfd_fdinfo_snapshot(int guest_fd, uint64_t *mask_out);
bool timerfd_fdinfo_snapshot(int guest_fd,
                             int *clockid_out,
                             uint64_t *ticks_out,
                             int64_t *value_ns_out,
                             int64_t *interval_ns_out);
