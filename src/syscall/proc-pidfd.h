/* pidfd helpers
 *
 * Copyright 2026 elfuse contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <stdint.h>

#include "core/guest.h"

int pidfd_create(guest_t *g, int64_t target_pid);
void proc_pidfd_notify_exit(int64_t exited_pid);
int64_t proc_pidfd_lookup_pid(int guest_fd);
int64_t sys_pidfd_open(guest_t *g, int64_t pid, unsigned int flags);
int64_t sys_pidfd_send_signal(guest_t *g,
                              int pidfd,
                              int sig,
                              uint64_t info_gva,
                              unsigned int flags);
