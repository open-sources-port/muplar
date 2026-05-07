/* Guest memory syscall interface
 *
 * Copyright 2026 elfuse contributors
 * Copyright 2025 Moritz Angermann, zw3rk pte. ltd.
 * SPDX-License-Identifier: Apache-2.0
 *
 * Declarations for brk, mmap, munmap, mprotect, mremap, madvise, msync.
 * All sys_xxx functions assume mmap_lock is held by the caller.
 */

#pragma once

#include <stdint.h>
#include "core/guest.h"

/* brk: set/query program break */
int64_t sys_brk(guest_t *g, uint64_t addr);

/* mmap: map pages into guest address space */
int64_t sys_mmap(guest_t *g,
                 uint64_t addr,
                 uint64_t length,
                 int prot,
                 int flags,
                 int fd,
                 int64_t offset);

/* munmap: unmap pages from guest address space */
int64_t sys_munmap(guest_t *g, uint64_t addr, uint64_t length);

/* mprotect: change page permissions */
int64_t sys_mprotect(guest_t *g, uint64_t addr, uint64_t length, int prot);

/* mremap: remap/resize a mapping */
int64_t sys_mremap(guest_t *g,
                   uint64_t old_addr,
                   uint64_t old_size,
                   uint64_t new_size,
                   int flags,
                   uint64_t new_addr);

/* madvise: advise about memory usage */
int64_t sys_madvise(guest_t *g, uint64_t addr, uint64_t length, int advice);

/* msync: synchronize file-backed mappings to disk */
int64_t sys_msync(guest_t *g, uint64_t addr, uint64_t length, int flags);
