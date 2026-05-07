/* Shared test utilities
 *
 * Copyright 2026 elfuse contributors
 * Copyright 2025 Moritz Angermann, zw3rk pte. ltd.
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <errno.h>
#include <fcntl.h>
#include <stddef.h>
#include <stdlib.h>
#include <unistd.h>

#include "raw-syscall.h"

static inline ssize_t read_fd_all_nul(int fd, char *buf, size_t bufsz)
{
    if (bufsz == 0)
        return -1;

    ssize_t total = 0;
    while ((size_t) total < bufsz - 1) {
        ssize_t n = read(fd, buf + total, bufsz - 1 - (size_t) total);
        if (n < 0 && errno == EINTR)
            continue;
        if (n <= 0)
            break;
        total += n;
    }
    buf[total] = '\0';
    return total;
}

static inline ssize_t read_file_nul(const char *path, char *buf, size_t bufsz)
{
    int fd = open(path, O_RDONLY);
    if (fd < 0)
        return -1;

    ssize_t total = read_fd_all_nul(fd, buf, bufsz);
    close(fd);
    return total;
}

static inline ssize_t raw_read_fd_all_nul(int fd, char *buf, size_t bufsz)
{
    if (bufsz == 0)
        return -1;

    ssize_t total = 0;
    while ((size_t) total < bufsz - 1) {
        long n = raw_syscall3(__NR_read, fd, (long) (buf + total),
                              (long) (bufsz - 1 - (size_t) total));
        if (n == -EINTR)
            continue;
        if (n <= 0)
            break;
        total += (ssize_t) n;
    }
    buf[total] = '\0';
    return total;
}

static inline int raw_open_rdonly(const char *path)
{
    return (int) raw_syscall4(__NR_openat, AT_FDCWD, (long) path, O_RDONLY, 0);
}

static inline int write_fd_all(int fd, const void *buf, size_t len)
{
    const char *p = (const char *) buf;
    size_t written = 0;

    while (written < len) {
        ssize_t n = write(fd, p + written, len - written);
        if (n < 0 && errno == EINTR)
            continue;
        if (n <= 0)
            return -1;
        written += (size_t) n;
    }

    return 0;
}

static inline void test_unreachable(void)
{
    abort();
#if defined(__GNUC__) || defined(__clang__)
    __builtin_unreachable();
#endif
}

static inline ssize_t raw_read_file_nul(const char *path,
                                        char *buf,
                                        size_t bufsz)
{
    int fd = raw_open_rdonly(path);
    if (fd < 0)
        return -1;

    ssize_t total = raw_read_fd_all_nul(fd, buf, bufsz);
    raw_syscall1(__NR_close, fd);
    return total;
}
