/* Structured crash report for GitHub issue filing
 *
 * Copyright 2026 elfuse contributors
 * Copyright 2025 Moritz Angermann, zw3rk pte. ltd.
 * SPDX-License-Identifier: Apache-2.0
 *
 * Prints a structured diagnostic to stderr when elfuse encounters a fatal
 * error. Sections: environment, crash type, binary info, registers,
 * memory layout, and instructions for filing a GitHub issue.
 */

#include <stdio.h>
#include <string.h>
#include <sys/sysctl.h>

#include "version.h"

#include "debug/crashreport.h"

#include "syscall/proc.h"

/* Read a sysctl string into buf (NUL-terminated). Returns 0 on success. */
static int sysctl_str(const char *name, char *buf, size_t bufsz)
{
    size_t len = bufsz;
    if (sysctlbyname(name, buf, &len, NULL, 0) != 0) {
        buf[0] = '\0';
        return -1;
    }
    buf[bufsz - 1] = '\0';
    return 0;
}

/* Map crash_type_t to a human-readable label. */
static const char *crash_type_name(crash_type_t type)
{
    switch (type) {
    case CRASH_TIMEOUT:
        return "TIMEOUT";
    case CRASH_BAD_EXCEPTION:
        return "BAD_EXCEPTION";
    case CRASH_UNEXPECTED_HVC:
        return "UNEXPECTED_HVC";
    case CRASH_UNEXPECTED_EC:
        return "UNEXPECTED_EC";
    case CRASH_UNEXPECTED_EXIT:
        return "UNEXPECTED_EXIT";
    case CRASH_ELR_ZERO:
        return "ELR_ZERO";
    case CRASH_HV_CHECK:
        return "HV_CHECK";
    }
    return "UNKNOWN";
}

/* Decode ESR_EL1 exception class (bits [31:26]) to a short label.
 * Values per ARM DDI 0487 (ARMv8-A Architecture Reference Manual).
 */
static const char *esr_ec_name(uint64_t esr)
{
    unsigned ec = (unsigned) ((esr >> 26) & 0x3f);
    switch (ec) {
    case 0x00:
        return "Unknown reason";
    case 0x01:
        return "WFI/WFE trapped";
    case 0x07:
        return "FP/ASIMD access trapped";
    case 0x0e:
        return "Illegal execution state";
    case 0x15:
        return "SVC from AArch64";
    case 0x16:
        return "HVC from AArch64";
    case 0x17:
        return "SMC from AArch64";
    case 0x18:
        return "MSR/MRS/System trap";
    case 0x19:
        return "SVE access trapped";
    case 0x20:
        return "Instruction abort (lower EL)";
    case 0x21:
        return "Instruction abort (same EL)";
    case 0x22:
        return "PC alignment fault";
    case 0x24:
        return "Data abort (lower EL)";
    case 0x25:
        return "Data abort (same EL)";
    case 0x26:
        return "SP alignment fault";
    case 0x2c:
        return "FP exception";
    case 0x30:
        return "HW breakpoint (lower EL)";
    case 0x31:
        return "HW breakpoint (same EL)";
    case 0x32:
        return "SW step (lower EL)";
    case 0x33:
        return "SW step (same EL)";
    case 0x34:
        return "HW watchpoint (lower EL)";
    case 0x35:
        return "HW watchpoint (same EL)";
    case 0x3c:
        return "BRK from AArch64";
    default:
        return "unhandled EC";
    }
}

void crash_report(hv_vcpu_t vcpu,
                  const guest_t *g,
                  crash_type_t type,
                  const char *detail)
{
    fprintf(stderr,
            "\n╔══════════════════════════════════════════════════════════╗\n"
            "║                   elfuse crash report                    ║\n"
            "╚══════════════════════════════════════════════════════════╝\n\n");

    char os_version[64] = {0}, os_release[64] = {0};
    char hw_model[128] = {0};

    sysctl_str("kern.osproductversion", os_version, sizeof(os_version));
    sysctl_str("kern.osrelease", os_release, sizeof(os_release));
    /* machdep.cpu.brand_string is absent on Apple Silicon. Fall back to
     * hw.model so the report still names the host hardware.
     */
    if (sysctl_str("machdep.cpu.brand_string", hw_model, sizeof(hw_model)) != 0)
        sysctl_str("hw.model", hw_model, sizeof(hw_model));

    fprintf(stderr, "## Environment\n");
    fprintf(stderr, "- elfuse version: %s\n", ELFUSE_VERSION);
    fprintf(stderr, "- macOS: %s (Darwin %s)\n",
            os_version[0] ? os_version : "?", os_release[0] ? os_release : "?");
    fprintf(stderr, "- hardware: %s\n\n",
            hw_model[0] ? hw_model : "Apple Silicon");

    fprintf(stderr, "## Crash\n");
    fprintf(stderr, "- type: %s\n", crash_type_name(type));
    if (detail && detail[0])
        fprintf(stderr, "- detail: %s\n", detail);
    fprintf(stderr, "\n");

    const char *elf_path = proc_get_elf_path();
    size_t cmdline_len = 0;
    const char *cmdline = proc_get_cmdline(&cmdline_len);
    const char *sysroot = proc_get_sysroot();

    fprintf(stderr, "## Binary\n");
    fprintf(stderr, "- path: %s\n", elf_path ? elf_path : "(unknown)");
    if (sysroot)
        fprintf(stderr, "- sysroot: %s\n", sysroot);

    /* proc_get_cmdline() stores the guest argv vector as a NUL-separated
     * blob. Re-expand it into shell-like spacing for the human report.
     */
    if (cmdline && cmdline_len > 0) {
        fprintf(stderr, "- cmdline:");
        size_t pos = 0;
        while (pos < cmdline_len) {
            fprintf(stderr, " %s", cmdline + pos);
            pos += strlen(cmdline + pos) + 1;
        }
        fprintf(stderr, "\n");
    }
    fprintf(stderr, "\n");

    if (vcpu) {
        fprintf(stderr, "## Registers\n");

        uint64_t pc = 0, cpsr = 0;
        hv_vcpu_get_reg(vcpu, HV_REG_PC, &pc);
        hv_vcpu_get_reg(vcpu, HV_REG_CPSR, &cpsr);
        fprintf(stderr, "PC   = 0x%016llx  CPSR = 0x%016llx\n",
                (unsigned long long) pc, (unsigned long long) cpsr);

        uint64_t esr = 0, far_reg = 0, elr = 0, spsr = 0, sctlr = 0, sp_el0 = 0,
                 tpidr = 0;
        hv_vcpu_get_sys_reg(vcpu, HV_SYS_REG_ESR_EL1, &esr);
        hv_vcpu_get_sys_reg(vcpu, HV_SYS_REG_FAR_EL1, &far_reg);
        hv_vcpu_get_sys_reg(vcpu, HV_SYS_REG_ELR_EL1, &elr);
        hv_vcpu_get_sys_reg(vcpu, HV_SYS_REG_SPSR_EL1, &spsr);
        hv_vcpu_get_sys_reg(vcpu, HV_SYS_REG_SCTLR_EL1, &sctlr);
        hv_vcpu_get_sys_reg(vcpu, HV_SYS_REG_SP_EL0, &sp_el0);
        hv_vcpu_get_sys_reg(vcpu, HV_SYS_REG_TPIDR_EL0, &tpidr);

        fprintf(stderr, "ESR  = 0x%016llx  EC=0x%02x (%s)\n",
                (unsigned long long) esr, (unsigned) ((esr >> 26) & 0x3f),
                esr_ec_name(esr));
        fprintf(stderr, "FAR  = 0x%016llx  ELR  = 0x%016llx\n",
                (unsigned long long) far_reg, (unsigned long long) elr);
        fprintf(stderr, "SPSR = 0x%016llx  SCTLR= 0x%016llx\n",
                (unsigned long long) spsr, (unsigned long long) sctlr);
        fprintf(stderr, "SP0  = 0x%016llx  TPIDR= 0x%016llx\n",
                (unsigned long long) sp_el0, (unsigned long long) tpidr);
        fprintf(stderr, "\n");

        /* Keep four registers per line so the report stays readable in issue
         * trackers and terminal captures.
         */
        for (int i = 0; i <= 30; i++) {
            uint64_t val = 0;
            hv_vcpu_get_reg(vcpu, (hv_reg_t) (HV_REG_X0 + i), &val);
            fprintf(stderr, "X%-2d  = 0x%016llx", i, (unsigned long long) val);
            if ((i % 4) == 3 || i == 30)
                fprintf(stderr, "\n");
            else
                fprintf(stderr, "  ");
        }
        fprintf(stderr, "\n");
    }

    if (g) {
        fprintf(stderr, "## Memory layout\n");
        fprintf(stderr, "guest_size  = 0x%llx (%llu MB, %u-bit IPA)\n",
                (unsigned long long) g->guest_size,
                (unsigned long long) (g->guest_size >> 20), g->ipa_bits);
        fprintf(stderr, "brk         = 0x%llx .. 0x%llx\n",
                (unsigned long long) g->brk_base,
                (unsigned long long) g->brk_current);
        fprintf(stderr, "mmap RW     = 0x%llx .. 0x%llx (next 0x%llx)\n",
                (unsigned long long) MMAP_BASE,
                (unsigned long long) g->mmap_end,
                (unsigned long long) g->mmap_next);
        fprintf(stderr, "mmap RX     = 0x%llx .. 0x%llx (next 0x%llx)\n",
                (unsigned long long) MMAP_RX_BASE,
                (unsigned long long) g->mmap_rx_end,
                (unsigned long long) g->mmap_rx_next);
        fprintf(stderr, "interp_base = 0x%llx  mmap_limit = 0x%llx\n",
                (unsigned long long) g->interp_base,
                (unsigned long long) g->mmap_limit);
        fprintf(stderr, "nregions    = %d\n\n", g->nregions);
    }

    fprintf(stderr,
            "## How to report\n"
            "File an issue at:\n"
            "  https://github.com/sysprog21/elfuse/issues/new\n\n"
            "Include in the issue:\n"
            "  1. The full crash report above (everything from the ╔ banner)\n"
            "  2. Full command line used to invoke elfuse\n"
            "  3. Static or dynamically linked? (file <binary> to check)\n"
            "  4. Reproducible? (always / sometimes / first-time-only)\n"
            "  5. The guest binary, if redistributable\n\n");
}
