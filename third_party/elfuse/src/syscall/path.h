/* Shared guest/host path handling
 *
 * Copyright 2026 elfuse contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/stat.h>

#include "syscall/internal.h"

bool path_might_use_open_intercept(const char *path);
bool path_might_use_stat_intercept(const char *path);
int path_check_intercept_access(const struct stat *st, int mode, int flags);
int resolve_proc_at_path(guest_fd_t dirfd,
                         const char *path,
                         char *out,
                         size_t outsz);
int resolve_proc_dirfd_path(guest_fd_t dirfd,
                            const char *path,
                            char *out,
                            size_t outsz);
int sys_path_has_symlink(guest_fd_t dirfd, const char *path);

const char *path_resolve_sysroot_path(const char *path,
                                      char *buf,
                                      size_t bufsz);
const char *path_resolve_sysroot_nofollow_path(const char *path,
                                               char *buf,
                                               size_t bufsz);
const char *path_resolve_sysroot_create_path(const char *path,
                                             char *buf,
                                             size_t bufsz);

int path_openat2_stays_beneath(const char *path, bool clamp_at_root);
int path_openat2_normalize_in_root(const char *path, char *out, size_t outsz);
int path_openat2_is_proc_magiclink(guest_fd_t dirfd, const char *path);
int path_openat2_resolved_within_root(guest_fd_t dirfd,
                                      const char *path,
                                      uint64_t oflags,
                                      bool in_root);
