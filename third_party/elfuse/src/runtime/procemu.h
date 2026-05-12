/* /proc and /dev path emulation
 *
 * Copyright 2026 elfuse contributors
 * Copyright 2025 Moritz Angermann, zw3rk pte. ltd.
 * SPDX-License-Identifier: Apache-2.0
 *
 * Intercepts openat and readlinkat for /proc, /dev, and other synthetic paths.
 * Returns host fds for synthetic content or -2 if not intercepted.
 */

#pragma once

#include <stddef.h>
#include <sys/stat.h>
#include <sys/uio.h>
#include "core/guest.h"

/* Sentinel return value: path was not intercepted, caller should fall through
 * to the real syscall.
 */
#define PROC_NOT_INTERCEPTED (-2)

/* Intercept openat for /proc and /dev paths.
 * The guest_t pointer is needed to generate /proc/self/maps from region data.
 * Returns a host fd on match (caller should fd_alloc it), -1 on error with
 * errno set, or PROC_NOT_INTERCEPTED if the path is not intercepted.
 */
int proc_intercept_open(const guest_t *g,
                        const char *path,
                        int linux_flags,
                        int mode);

/* Intercept readlinkat for /proc paths.
 * Returns the link length on match, -1 on error, or -2 if not intercepted (fall
 * through to real readlinkat).
 */
int proc_intercept_readlink(const char *path, char *buf, size_t bufsiz);

/* Intercept stat/fstatat for /proc paths.
 * Returns 0 if stat was synthesized (mac_st filled), or -2 if not intercepted
 * (fall through to real fstatat).
 */
int proc_intercept_stat(const char *path, struct stat *mac_st);

/* Intercept writes to synthetic proc files that need stateful behavior.
 * Returns 1 if handled (with *written_out set), 0 if not intercepted, or -1
 * on error with errno set.
 */
int proc_intercept_write(int guest_fd,
                         int host_fd,
                         const void *buf,
                         size_t count,
                         int64_t offset,
                         int use_pwrite,
                         ssize_t *written_out);

/* Intercept reads from synthetic proc files that must reflect shared state on
 * every read rather than the per-open temp-file snapshot.
 * Returns 1 if handled (with *read_out set), 0 if not intercepted, or -1 on
 * error with errno set.
 */
int proc_intercept_read(int guest_fd,
                        void *buf,
                        size_t count,
                        int64_t offset,
                        ssize_t *read_out);

/* Vector form of proc_intercept_read for readv/preadv. */
int proc_intercept_readv(int guest_fd,
                         const struct iovec *iov,
                         int iovcnt,
                         int64_t offset,
                         ssize_t *read_out);

/* Get the /dev/shm emulation directory path (creating it on first call).
 * Used by sys_unlinkat to rewrite /dev/shm/<name> paths.
 */
const char *proc_get_shm_dir(void);

/* Resolve a /dev/shm/<suffix> guest-path suffix to a host path inside the
 * per-UID /dev/shm emulation directory. Rejects empty, ".."-bearing, or
 * compound suffixes with errno = EACCES. Returns 0 on success and writes the
 * full host path to host_path; returns -1 with errno set (EACCES on invalid
 * suffix, ENAMETOOLONG on overflow, or as set by shm_dir_path()).
 */
int proc_dev_shm_resolve(const char *guest_suffix,
                         char *host_path,
                         size_t host_path_sz);
