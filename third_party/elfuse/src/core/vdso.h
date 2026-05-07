/* Minimal vDSO (Virtual Dynamic Shared Object)
 *
 * Copyright 2026 elfuse contributors
 * Copyright 2025 Moritz Angermann, zw3rk pte. ltd.
 * SPDX-License-Identifier: Apache-2.0
 *
 * Provides a minimal vDSO ELF image for the guest. The vDSO provides
 * __kernel_rt_sigreturn (signal frame restorer) and standard Linux kernel
 * helper functions. Mapped into guest memory; address provided via
 * AT_SYSINFO_EHDR in the auxiliary vector.
 */

#pragma once

#include "core/guest.h"

/* Guest address where the vDSO is placed (one 4KiB page, below PT pool) */
#define VDSO_BASE 0x0000F000ULL
#define VDSO_SIZE 0x00001000ULL /* 4KiB */
#define VDSO_OFF_TEXT 0x0B0     /* Offset of .text (trampoline code) */

/* Build a minimal vDSO ELF image at VDSO_BASE in guest memory.
 * The image contains a valid ELF header, one LOAD program header, SHT_DYNSYM
 * and SHT_STRTAB sections, and a __kernel_rt_sigreturn symbol pointing to
 * a small trampoline (mov x8, #139; svc #0).
 * Returns the GVA of the ELF header (== VDSO_BASE), or 0 on failure.
 */
uint64_t vdso_build(guest_t *g);
