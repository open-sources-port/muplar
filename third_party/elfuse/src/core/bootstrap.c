/* Guest bootstrap helpers
 *
 * Copyright 2026 elfuse contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#include <Hypervisor/Hypervisor.h>
#include <Hypervisor/hv_vcpu.h>
#include <libkern/OSCacheControl.h>
#include <mach-o/dyld.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "hvutil.h"
#include "utils.h"

#include "core/bootstrap.h"
#include "core/stack.h"
#include "core/vdso.h"

#include "runtime/thread.h"

#include "syscall/abi.h"
#include "syscall/internal.h"
#include "syscall/proc.h"

#include "debug/log.h"

/* Worst case: 7 fixed regions (shim, shim-data, vDSO, brk, stack, mmap RX, mmap
 * RW) plus up to ELF_MAX_SEGMENTS for both the executable and the interpreter.
 */
#define MAX_BOOT_REGIONS (8 + 2 * ELF_MAX_SEGMENTS)

static bool append_boot_region(mem_region_t *regions,
                               int *nregions,
                               uint64_t gpa_start,
                               uint64_t gpa_end,
                               int perms)
{
    if (*nregions >= MAX_BOOT_REGIONS)
        return false;

    regions[*nregions] = (mem_region_t) {
        .gpa_start = gpa_start, .gpa_end = gpa_end, .perms = perms};
    (*nregions)++;
    return true;
}

static void invalidate_exec_segments(const elf_info_t *info,
                                     void *host_base,
                                     uint64_t load_base)
{
    for (int i = 0; i < info->num_segments; i++) {
        if (info->segments[i].flags & PF_X) {
            void *host_addr =
                (uint8_t *) host_base + info->segments[i].gpa + load_base;
            sys_icache_invalidate(host_addr, info->segments[i].memsz);
        }
    }
}

static void log_initial_page_tables(const guest_t *g, uint64_t ttbr0)
{
    uint64_t l0_off = ttbr0 - g->ipa_base;
    uint64_t *l0 = (uint64_t *) ((uint8_t *) g->host_base + l0_off);
    unsigned l0i = (unsigned) (g->ipa_base / (512ULL * 1024 * 1024 * 1024));
    log_debug("L0[%u]=0x%llx", l0i, (unsigned long long) l0[l0i]);

    uint64_t l1_ipa = l0[l0i] & 0xFFFFFFFFF000ULL;
    uint64_t *l1 =
        (uint64_t *) ((uint8_t *) g->host_base + (l1_ipa - g->ipa_base));
    unsigned l1i = (unsigned) ((g->ipa_base % (512ULL * 1024 * 1024 * 1024)) /
                               (1024ULL * 1024 * 1024));
    log_debug("L1[%u]=0x%llx", l1i, (unsigned long long) l1[l1i]);

    uint64_t l2_ipa = l1[l1i] & 0xFFFFFFFFF000ULL;
    uint64_t *l2 =
        (uint64_t *) ((uint8_t *) g->host_base + (l2_ipa - g->ipa_base));
    for (int i = 0; i < 16; i++) {
        if (l2[i])
            log_debug("L2[%d]=0x%llx", i, (unsigned long long) l2[i]);
    }
}

static bool load_interpreter(guest_t *g,
                             const char *sysroot,
                             guest_bootstrap_t *boot)
{
    if (boot->elf_info.interp_path[0] == '\0')
        return true;

    elf_resolve_interp(sysroot, boot->elf_info.interp_path,
                       boot->interp_resolved, sizeof(boot->interp_resolved));
    log_debug("loading interpreter: %s", boot->interp_resolved);

    if (elf_load(boot->interp_resolved, &boot->interp_info) < 0) {
        log_error("failed to load interpreter: %s", boot->interp_resolved);
        return false;
    }

    if (boot->interp_info.e_machine != EM_AARCH64) {
        log_error("interpreter has unsupported machine type %u: %s",
                  boot->interp_info.e_machine, boot->interp_resolved);
        return false;
    }

    boot->interp_base = g->interp_base;
    if (elf_map_segments(&boot->interp_info, boot->interp_resolved,
                         g->host_base, g->guest_size, boot->interp_base) < 0) {
        log_error("failed to map interpreter segments");
        return false;
    }

    log_debug(
        "interpreter loaded at base=0x%llx, entry=0x%llx, %d segments",
        (unsigned long long) boot->interp_base,
        (unsigned long long) (boot->interp_info.entry + boot->interp_base),
        boot->interp_info.num_segments);
    return true;
}

static bool build_boot_regions(mem_region_t *regions,
                               int *nregions,
                               guest_t *g,
                               const guest_bootstrap_t *boot,
                               size_t shim_bin_len)
{
    /* The vDSO trampolines live in the same 2MiB block as the shim. They must
     * appear in the region set so finalize_block_perms validates and grants RX
     * to the vDSO page when splitting the block; otherwise vdso_build cannot
     * write into it through guest_ptr.
     */
    if (!append_boot_region(regions, nregions, g->shim_base,
                            g->shim_base + shim_bin_len, MEM_PERM_RX) ||
        !append_boot_region(regions, nregions, g->shim_data_base,
                            g->shim_data_base + BLOCK_2MIB, MEM_PERM_RW) ||
        !append_boot_region(regions, nregions, VDSO_BASE, VDSO_BASE + VDSO_SIZE,
                            MEM_PERM_RX)) {
        return false;
    }

    for (int i = 0; i < boot->elf_info.num_segments; i++) {
        if (!append_boot_region(
                regions, nregions,
                boot->elf_info.segments[i].gpa + boot->elf_load_base,
                boot->elf_info.segments[i].gpa +
                    boot->elf_info.segments[i].memsz + boot->elf_load_base,
                elf_pf_to_prot(boot->elf_info.segments[i].flags))) {
            return false;
        }
    }

    for (int i = 0; i < boot->interp_info.num_segments; i++) {
        if (!append_boot_region(
                regions, nregions,
                boot->interp_info.segments[i].gpa + boot->interp_base,
                boot->interp_info.segments[i].gpa +
                    boot->interp_info.segments[i].memsz + boot->interp_base,
                elf_pf_to_prot(boot->interp_info.segments[i].flags))) {
            return false;
        }
    }

    if (!append_boot_region(regions, nregions, g->brk_base, MMAP_RX_BASE,
                            MEM_PERM_RW) ||
        !append_boot_region(regions, nregions, g->stack_base, g->stack_top,
                            MEM_PERM_RW) ||
        !append_boot_region(regions, nregions, MMAP_RX_BASE,
                            MMAP_RX_INITIAL_END, MEM_PERM_RX) ||
        !append_boot_region(regions, nregions, MMAP_BASE, MMAP_INITIAL_END,
                            MEM_PERM_RW)) {
        return false;
    }

    g->mmap_rx_end = MMAP_RX_INITIAL_END;
    g->mmap_end = MMAP_INITIAL_END;
    return true;
}

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
                            guest_bootstrap_t *boot)
{
    mem_region_t regions[MAX_BOOT_REGIONS];
    int nregions = 0;
    uint64_t native_vdso;

    memset(boot, 0, sizeof(*boot));
    *guest_initialized = false;

    if (elf_load(elf_path, &boot->elf_info) < 0) {
        log_error("failed to load ELF: %s", elf_path);
        return -1;
    }

    if (boot->elf_info.e_machine != EM_AARCH64) {
        log_error("unsupported ELF machine type %u (only aarch64 is supported)",
                  boot->elf_info.e_machine);
        return -1;
    }

    log_debug(
        "ELF entry=0x%llx, %d segments, load range [0x%llx, 0x%llx), "
        "machine=aarch64",
        (unsigned long long) boot->elf_info.entry, boot->elf_info.num_segments,
        (unsigned long long) boot->elf_info.load_min,
        (unsigned long long) boot->elf_info.load_max);

    if (guest_init(g, 0, 0) < 0) {
        log_error("failed to initialize guest");
        return -1;
    }
    *guest_initialized = true;

    log_debug("IPA size: %u bits (%llu GiB primary)", g->ipa_bits,
              (unsigned long long) (g->guest_size / (1024ULL * 1024 * 1024)));

    boot->elf_load_base = (boot->elf_info.e_type == ET_DYN) ? PIE_LOAD_BASE : 0;
    if (elf_map_segments(&boot->elf_info, elf_path, g->host_base, g->guest_size,
                         boot->elf_load_base) < 0) {
        log_error("failed to map ELF segments");
        return -1;
    }

    /* Track the lowest loaded ELF address so the legacy fork IPC path
     * copies low-linked ET_EXECs (e.g. linked at 0x200000) in full.
     */
    g->elf_load_min = boot->elf_info.load_min + boot->elf_load_base;

    g->brk_base = PAGE_ALIGN_UP(boot->elf_info.load_max + boot->elf_load_base);
    if (g->brk_base < BRK_BASE_DEFAULT)
        g->brk_base = BRK_BASE_DEFAULT;
    g->brk_current = g->brk_base;

    g->stack_top = ALIGN_UP(g->brk_base, BLOCK_2MIB) + STACK_SIZE;
    if (g->stack_top < STACK_TOP_DEFAULT)
        g->stack_top = STACK_TOP_DEFAULT;
    g->stack_base = g->stack_top - STACK_SIZE;

    if (!load_interpreter(g, sysroot, boot))
        return -1;

    if (shim_bin_len > BLOCK_2MIB) {
        log_error("shim binary too large (%zu bytes)", shim_bin_len);
        return -1;
    }

    memcpy((uint8_t *) g->host_base + g->shim_base, shim_bin, shim_bin_len);
    log_debug("shim loaded at offset 0x%llx (%zu bytes)",
              (unsigned long long) g->shim_base, shim_bin_len);

    invalidate_exec_segments(&boot->elf_info, g->host_base,
                             boot->elf_load_base);
    invalidate_exec_segments(&boot->interp_info, g->host_base,
                             boot->interp_base);
    sys_icache_invalidate((uint8_t *) g->host_base + g->shim_base,
                          shim_bin_len);

    if (!build_boot_regions(regions, &nregions, g, boot, shim_bin_len)) {
        log_error("too many memory regions (%d >= %d)", nregions,
                  MAX_BOOT_REGIONS);
        return -1;
    }

    boot->ttbr0 = guest_build_page_tables(g, regions, nregions);
    if (!boot->ttbr0) {
        log_error("failed to build page tables");
        return -1;
    }
    /* No TLBI request here: the shim's _start does TLBI VMALLE1IS before
     * enabling the MMU (src/core/shim.S), and the per-vCPU accumulator is the
     * wrong place to stage a bring-up flush -- bootstrap may run on a thread
     * whose slot is later consumed by an unrelated syscall.
     */

    guest_region_add(g, g->shim_base, g->shim_base + shim_bin_len,
                     LINUX_PROT_READ | LINUX_PROT_EXEC, LINUX_MAP_PRIVATE, 0,
                     "[shim]");
    guest_region_add(g, g->shim_data_base, g->shim_data_base + BLOCK_2MIB,
                     LINUX_PROT_READ | LINUX_PROT_WRITE, LINUX_MAP_PRIVATE, 0,
                     "[shim-data]");

    {
        char elf_realpath[LINUX_PATH_MAX];

        memset(elf_realpath, 0, sizeof(elf_realpath));
        if (!realpath(elf_path, elf_realpath))
            str_copy_trunc(elf_realpath, elf_path, sizeof(elf_realpath));

        for (int i = 0; i < boot->elf_info.num_segments; i++) {
            guest_region_add(
                g, boot->elf_info.segments[i].gpa + boot->elf_load_base,
                boot->elf_info.segments[i].gpa +
                    boot->elf_info.segments[i].memsz + boot->elf_load_base,
                elf_pf_to_prot(boot->elf_info.segments[i].flags),
                LINUX_MAP_PRIVATE, boot->elf_info.segments[i].offset,
                elf_realpath);
        }
    }

    for (int i = 0; i < boot->interp_info.num_segments; i++) {
        guest_region_add(
            g, boot->interp_info.segments[i].gpa + boot->interp_base,
            boot->interp_info.segments[i].gpa +
                boot->interp_info.segments[i].memsz + boot->interp_base,
            elf_pf_to_prot(boot->interp_info.segments[i].flags),
            LINUX_MAP_PRIVATE, boot->interp_info.segments[i].offset,
            boot->interp_resolved);
    }

    if (g->brk_base < g->brk_current) {
        guest_region_add(g, g->brk_base, g->brk_current,
                         LINUX_PROT_READ | LINUX_PROT_WRITE,
                         LINUX_MAP_PRIVATE | LINUX_MAP_ANONYMOUS, 0, "[heap]");
    }

    guest_invalidate_ptes(g, g->stack_base, g->stack_base + STACK_GUARD_SIZE);
    guest_region_add(g, g->stack_base, g->stack_base + STACK_GUARD_SIZE,
                     LINUX_PROT_NONE, LINUX_MAP_PRIVATE | LINUX_MAP_ANONYMOUS,
                     0, "[stack-guard]");
    guest_region_add(g, g->stack_base + STACK_GUARD_SIZE, g->stack_top,
                     LINUX_PROT_READ | LINUX_PROT_WRITE,
                     LINUX_MAP_PRIVATE | LINUX_MAP_ANONYMOUS, 0, "[stack]");
    guest_invalidate_ptes(g, 0, 0x1000);

    log_debug("TTBR0=0x%llx, IPA base=0x%llx", (unsigned long long) boot->ttbr0,
              (unsigned long long) g->ipa_base);
    if (verbose)
        log_initial_page_tables(g, boot->ttbr0);

    syscall_init();
    proc_init();

    {
        char self_path[LINUX_PATH_MAX];
        uint32_t path_len = sizeof(self_path);

        if (_NSGetExecutablePath(self_path, &path_len) == 0)
            proc_set_elfuse_path(self_path);
    }

    proc_set_shim(shim_bin, (unsigned int) shim_bin_len);
    proc_set_elf_path(elf_path);
    if (sysroot)
        proc_set_sysroot(sysroot);
    proc_set_cmdline(guest_argc, guest_argv);
    proc_set_environ((const char **) environ);

    native_vdso = vdso_build(g);
    linux_stack_auxv_t auxv;
    boot->stack_pointer = build_linux_stack(
        g, g->stack_top, guest_argc, guest_argv, (const char **) environ,
        &boot->elf_info, boot->elf_load_base, boot->interp_base, native_vdso,
        -1, &auxv);
    if (boot->stack_pointer == 0) {
        log_error("failed to build initial stack");
        return -1;
    }

    proc_set_auxv(auxv.words, auxv.nwords * sizeof(auxv.words[0]));
    boot->entry_point = (boot->interp_base != 0)
                            ? (boot->interp_info.entry + boot->interp_base)
                            : (boot->elf_info.entry + boot->elf_load_base);

    log_debug("SP=0x%llx, entry=0x%llx%s",
              (unsigned long long) boot->stack_pointer,
              (unsigned long long) boot->entry_point,
              boot->interp_base ? " (via interpreter)" : "");
    return 0;
}

int guest_bootstrap_create_vcpu(guest_t *g,
                                const guest_bootstrap_t *boot,
                                bool verbose,
                                hv_vcpu_t *out_vcpu,
                                hv_vcpu_exit_t **out_vexit)
{
    uint64_t sctlr;
    uint64_t sctlr_with_mmu;
    uint64_t tcr_value = TCR_EL1_VALUE;
    uint64_t shim_ipa = guest_ipa(g, g->shim_base);
    uint64_t entry_ipa = guest_ipa(g, boot->entry_point);
    uint64_t sp_ipa = guest_ipa(g, boot->stack_pointer);
    uint64_t el1_sp = guest_ipa(g, g->shim_data_base + BLOCK_2MIB);
    hv_vcpu_t vcpu;
    hv_vcpu_exit_t *vexit;

    HV_CHECK(hv_vcpu_create(&vcpu, &vexit, NULL));
    g->vcpu = vcpu;
    g->exit = vexit;
    *out_vcpu = vcpu;
    *out_vexit = vexit;

    thread_register_main(vcpu, vexit, proc_get_pid(), el1_sp);

    HV_CHECK(hv_vcpu_set_sys_reg(vcpu, HV_SYS_REG_VBAR_EL1, shim_ipa + 0x800));
    HV_CHECK(hv_vcpu_set_sys_reg(vcpu, HV_SYS_REG_MAIR_EL1, 0xFF00));
    HV_CHECK(hv_vcpu_set_sys_reg(vcpu, HV_SYS_REG_TCR_EL1, tcr_value));
    HV_CHECK(hv_vcpu_set_sys_reg(vcpu, HV_SYS_REG_TTBR0_EL1, boot->ttbr0));
    HV_CHECK(hv_vcpu_set_sys_reg(vcpu, HV_SYS_REG_TTBR1_EL1, 0));
    HV_CHECK(hv_vcpu_set_sys_reg(vcpu, HV_SYS_REG_CPACR_EL1, 3ULL << 20));
    HV_CHECK(hv_vcpu_set_sys_reg(vcpu, HV_SYS_REG_ELR_EL1, entry_ipa));
    HV_CHECK(hv_vcpu_set_sys_reg(vcpu, HV_SYS_REG_SPSR_EL1, 0x0));
    HV_CHECK(hv_vcpu_set_sys_reg(vcpu, HV_SYS_REG_SP_EL0, sp_ipa));
    HV_CHECK(hv_vcpu_set_sys_reg(vcpu, HV_SYS_REG_SP_EL1, el1_sp));

    HV_CHECK(hv_vcpu_get_sys_reg(vcpu, HV_SYS_REG_SCTLR_EL1, &sctlr));
    log_debug("SCTLR_EL1 default=0x%llx", (unsigned long long) sctlr);

    sctlr = SCTLR_RES1 | SCTLR_C | SCTLR_I | SCTLR_DZE | SCTLR_UCT | SCTLR_UCI;
    HV_CHECK(hv_vcpu_set_sys_reg(vcpu, HV_SYS_REG_SCTLR_EL1, sctlr));

    HV_CHECK(hv_vcpu_set_reg(vcpu, HV_REG_PC, shim_ipa));
    HV_CHECK(hv_vcpu_set_reg(vcpu, HV_REG_CPSR, 0x3c5));
    vcpu_zero_gprs(vcpu);

    sctlr_with_mmu = SCTLR_RES1 | SCTLR_M | SCTLR_C | SCTLR_I | SCTLR_DZE |
                     SCTLR_UCT | SCTLR_UCI;
    HV_CHECK(hv_vcpu_set_reg(vcpu, HV_REG_X0, sctlr_with_mmu));

    log_debug(
        "vCPU configured: PC=0x%llx SCTLR=0x%llx VBAR=0x%llx TTBR0=0x%llx "
        "TCR=0x%llx",
        (unsigned long long) shim_ipa, (unsigned long long) sctlr,
        (unsigned long long) (shim_ipa + 0x800),
        (unsigned long long) boot->ttbr0, (unsigned long long) tcr_value);
    log_debug("ELR_EL1=0x%llx SP_EL0=0x%llx SP_EL1=0x%llx",
              (unsigned long long) entry_ipa, (unsigned long long) sp_ipa,
              (unsigned long long) el1_sp);

    if (verbose)
        log_debug("main thread registered with SP_EL1=0x%llx",
                  (unsigned long long) el1_sp);

    /* guest_build_page_tables and the bootstrap-time guest_invalidate_ptes
     * calls (stack guard, null page, etc.) accumulate TLBI requests on this
     * (the main) thread's cpu_tlbi_req TLS slot. The shim's _start does TLBI
     * VMALLE1IS before enabling the MMU, so any TLB state was already dropped
     * before guest code runs. Clear the accumulator so the first guest syscall
     * does not redundantly broadcast on top of that.
     */
    tlbi_request_clear();

    return 0;
}
