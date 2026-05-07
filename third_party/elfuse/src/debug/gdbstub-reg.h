/* GDB register snapshot helpers
 *
 * Copyright 2026 elfuse contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <stddef.h>
#include <stdint.h>

#include "runtime/thread.h"

#define GDBREGOFFGPR(n) ((size_t) (n) * 8)
#define GDBREGOFFSP ((size_t) 31 * 8)
#define GDBREGOFFPC ((size_t) 32 * 8)
#define GDBREGOFFCPSR ((size_t) 33 * 8)
#define GDBREGOFFV(n) ((size_t) 268 + (size_t) (n) * 16)
#define GDBREGOFFFPSR ((size_t) 268 + (size_t) 32 * 16)
#define GDBREGOFFFPCR ((size_t) 784)
#define GDBREGSNAPSIZE ((size_t) 788)

void gdb_snap_vcpu(thread_entry_t *t);
void gdb_restore_vcpu(thread_entry_t *t, int tde_stop);
int gdb_reg_offset(uint64_t regnum, int *out_off, int *out_size);
int gdb_reply_target_xml(int fd, const char *pkt);
