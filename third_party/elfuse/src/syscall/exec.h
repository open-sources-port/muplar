/* execve syscall handler
 *
 * Copyright 2026 elfuse contributors
 * Copyright 2025 Moritz Angermann, zw3rk pte. ltd.
 * SPDX-License-Identifier: Apache-2.0
 *
 * Implements execve: reads path/argv/envp from guest memory, reloads ELF,
 * resets guest state, rebuilds page tables, and restarts at new entry point.
 */

#pragma once

#include <Hypervisor/Hypervisor.h>
#include <stdbool.h>
#include <stdint.h>
#include "core/guest.h"

/* Execute a new binary, replacing current process image.
 * Reads path, argv[], envp[] from guest memory, reloads ELF, resets state.
 * Returns SYSCALL_EXEC_HAPPENED on success (caller skips X0 write), or negative
 * Linux errno on failure.
 */
int64_t sys_execve(hv_vcpu_t vcpu,
                   guest_t *g,
                   uint64_t path_gva,
                   uint64_t argv_gva,
                   uint64_t envp_gva,
                   bool verbose,
                   const char *host_path);
