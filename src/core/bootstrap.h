/* Guest bootstrap helpers
 *
 * Copyright 2026 elfuse contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <Hypervisor/Hypervisor.h>
#include <Hypervisor/hv_vcpu.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "core/elf.h"
#include "core/guest.h"
#include "syscall/internal.h"

typedef struct {
    elf_info_t elf_info;
    elf_info_t interp_info;
    char interp_resolved[LINUX_PATH_MAX];
    uint64_t elf_load_base;
    uint64_t interp_base;
    uint64_t ttbr0;
    uint64_t stack_pointer;
    uint64_t entry_point;
} guest_bootstrap_t;

int guest_bootstrap_prepare(guest_t *g,
                            const char *elf_path,
                            const char *sysroot,
                            int guest_argc,
                            const char **guest_argv,
                            char **environ,
                            const unsigned char *shim_bin,
                            size_t shim_bin_len,
                            bool verbose,
                            bool *guest_initialized,
                            guest_bootstrap_t *boot);

int guest_bootstrap_create_vcpu(guest_t *g,
                                const guest_bootstrap_t *boot,
                                bool verbose,
                                hv_vcpu_t *out_vcpu,
                                hv_vcpu_exit_t **out_vexit);
