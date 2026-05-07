/* Core I/O syscall handlers
 *
 * Copyright 2026 elfuse contributors
 * Copyright 2025 Moritz Angermann, zw3rk pte. ltd.
 * SPDX-License-Identifier: Apache-2.0
 *
 * Read/write, ioctl, splice, sendfile, and copy_file_range operations.
 * Translates Linux aarch64 I/O syscalls into macOS equivalents, handling
 * terminal attribute translation and pipe splice emulation.
 *
 * Poll/select/epoll declarations are in syscall/poll.h.
 * Special FD types (eventfd, signalfd, timerfd) are in syscall/fd.h.
 */

#pragma once

#include <stdint.h>
#include "core/guest.h"

/* I/O syscall handlers. */

/* read/write and their positional variants. */
int64_t sys_write(guest_t *g, int fd, uint64_t buf_gva, uint64_t count);
int64_t sys_read(guest_t *g, int fd, uint64_t buf_gva, uint64_t count);
int64_t sys_pread64(guest_t *g,
                    int fd,
                    uint64_t buf_gva,
                    uint64_t count,
                    int64_t offset);
int64_t sys_pwrite64(guest_t *g,
                     int fd,
                     uint64_t buf_gva,
                     uint64_t count,
                     int64_t offset);
int64_t sys_readv(guest_t *g, int fd, uint64_t iov_gva, int iovcnt);
int64_t sys_writev(guest_t *g, int fd, uint64_t iov_gva, int iovcnt);
int64_t sys_preadv(guest_t *g,
                   int fd,
                   uint64_t iov_gva,
                   int iovcnt,
                   int64_t offset);
int64_t sys_pwritev(guest_t *g,
                    int fd,
                    uint64_t iov_gva,
                    int iovcnt,
                    int64_t offset);
int64_t sys_preadv2(guest_t *g,
                    int fd,
                    uint64_t iov_gva,
                    int iovcnt,
                    int64_t offset,
                    int flags);
int64_t sys_pwritev2(guest_t *g,
                     int fd,
                     uint64_t iov_gva,
                     int iovcnt,
                     int64_t offset,
                     int flags);

/* terminal I/O */
int64_t sys_ioctl(guest_t *g, int fd, uint64_t request, uint64_t arg);

/* file space/copy */
int64_t sys_fallocate(int fd, int mode, int64_t offset, int64_t len);
int64_t sys_sendfile(guest_t *g,
                     int out_fd,
                     int in_fd,
                     uint64_t offset_gva,
                     uint64_t count);
int64_t sys_copy_file_range(guest_t *g,
                            int fd_in,
                            uint64_t off_in_gva,
                            int fd_out,
                            uint64_t off_out_gva,
                            uint64_t len,
                            unsigned int flags);

/* splice/tee */
int64_t sys_splice(guest_t *g,
                   int fd_in,
                   uint64_t off_in_gva,
                   int fd_out,
                   uint64_t off_out_gva,
                   size_t len,
                   unsigned int flags);
int64_t sys_vmsplice(guest_t *g,
                     int fd,
                     uint64_t iov_gva,
                     unsigned long nr_segs,
                     unsigned int flags);
int64_t sys_tee(int fd_in, int fd_out, size_t len, unsigned int flags);
