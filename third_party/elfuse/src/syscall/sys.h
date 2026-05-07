/* System info and identity syscall handlers
 *
 * Copyright 2026 elfuse contributors
 * Copyright 2025 Moritz Angermann, zw3rk pte. ltd.
 * SPDX-License-Identifier: Apache-2.0
 *
 * Uname, getrandom, getcwd, sched_getaffinity, getgroups, getrusage,
 * sysinfo, and prlimit64. System queries that do not fit neatly into
 * filesystem, I/O, or time categories.
 */

#pragma once

#include <stdint.h>
#include "core/guest.h"

/* System info syscall handlers. */

int64_t sys_uname(guest_t *g, uint64_t buf_gva);
int64_t sys_getrandom(guest_t *g,
                      uint64_t buf_gva,
                      uint64_t buflen,
                      unsigned int flags);
int64_t sys_getcwd(guest_t *g, uint64_t buf_gva, uint64_t size);
int64_t sys_sched_getaffinity(guest_t *g,
                              int pid,
                              uint64_t size,
                              uint64_t mask_gva);
int64_t sys_getgroups(guest_t *g, int size, uint64_t list_gva);
int64_t sys_getrusage(guest_t *g, int who, uint64_t usage_gva);
int64_t sys_sysinfo(guest_t *g, uint64_t info_gva);
int64_t sys_prlimit64(guest_t *g,
                      int pid,
                      int resource,
                      uint64_t new_gva,
                      uint64_t old_gva);

/* Format /proc/self/limits content into buf.  Returns bytes written
 * (excluding NUL) or -1 on error.
 */
int sys_format_limits(char *buf, size_t bufsz);
