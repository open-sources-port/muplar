/* Shared Hypervisor.framework utility macros and constants
 *
 * Copyright 2026 elfuse contributors
 * SPDX-License-Identifier: Apache-2.0
 *
 * Provides HV_CHECK macro and SCTLR_EL1 constants, centralizing these
 * definitions to avoid duplication.
 */

#pragma once

#include <Hypervisor/Hypervisor.h>
#include <stdio.h>
#include <stdlib.h>

/* HV_CHECK macro.
 * Abort on HVF API failure. Used for calls that should never fail
 * during normal operation (vCPU register access, VM mapping).
 */
#define HV_CHECK(call)                                                   \
    do {                                                                 \
        hv_return_t _r = (call);                                         \
        if (_r != HV_SUCCESS) {                                          \
            fprintf(stderr, "elfuse: %s failed: %d\n", #call, (int) _r); \
            exit(1);                                                     \
        }                                                                \
    } while (0)

/* HV_CHECK variant that emits a structured crash report before exit.
 * Use in the vCPU run loop where vcpu and guest_t are available.
 */
#define HV_CHECK_CTX(call, vcpu, g)                                      \
    do {                                                                 \
        hv_return_t _r = (call);                                         \
        if (_r != HV_SUCCESS) {                                          \
            fprintf(stderr, "elfuse: %s failed: %d\n", #call, (int) _r); \
            crash_report((vcpu), (g), CRASH_HV_CHECK, #call);            \
            exit(1);                                                     \
        }                                                                \
    } while (0)

/* SCTLR_EL1 bits.
 * These are needed by main.c (initial setup), runtime/forkipc.c (child MMU
 * enable), and syscall/exec.c (exec MMU re-enable).
 */
#define SCTLR_M (1ULL << 0)    /* MMU enable */
#define SCTLR_C (1ULL << 2)    /* Data cache enable */
#define SCTLR_I (1ULL << 12)   /* Instruction cache enable */
#define SCTLR_DZE (1ULL << 14) /* EL0 access to DC ZVA instruction */
#define SCTLR_UCT (1ULL << 15) /* EL0 access to CTR_EL0 (cache type) */
#define SCTLR_UCI (1ULL << 26) /* EL0 cache maintenance (IC IVAU, DC CVA*) */

/* RES1 bits in SCTLR_EL1: these MUST be 1 for correct behavior.
 * Apple Hypervisor.framework returns default SCTLR=0x0, so the setup code must
 * set them explicitly. The hardware eventually auto-updates them, but the
 * initial instruction fetches execute with whatever the setup code wrote.
 */
#define SCTLR_RES1                                                   \
    ((1ULL << 29) /* LSMAOE */ | (1ULL << 28) /* nTLSMD */ |         \
     (1ULL << 23) /* SPAN, disables auto-PAN on exception entry */ | \
     (1ULL << 22) /* EIS    */ | (1ULL << 20) /* TSCXT  */ |         \
     (1ULL << 11) /* EOS    */ | (1ULL << 8) /* SED    */ |          \
     (1ULL << 7) /* ITD    */)

/* TCR_EL1.
 * 4KiB granule, 48-bit VA, EPD1=1 (TTBR1 walks disabled).
 * Used by main.c (initial setup) and syscall/exec.c (exec re-init).
 */
#define TCR_EL1_VALUE 0x25B5903510ULL

/* vCPU register helpers.
 *
 * Thin wrappers around hv_vcpu_{get,set}_reg/sys_reg with internal
 * HV_CHECK. Reduces boilerplate and centralizes error handling.
 * Use these for new code; existing call sites can migrate gradually.
 */

static inline uint64_t vcpu_get_gpr(hv_vcpu_t vcpu, unsigned n)
{
    uint64_t val;
    HV_CHECK(hv_vcpu_get_reg(vcpu, HV_REG_X0 + n, &val));
    return val;
}

static inline void vcpu_set_gpr(hv_vcpu_t vcpu, unsigned n, uint64_t val)
{
    HV_CHECK(hv_vcpu_set_reg(vcpu, HV_REG_X0 + n, val));
}

static inline uint64_t vcpu_get_reg(hv_vcpu_t vcpu, hv_reg_t reg)
{
    uint64_t val;
    HV_CHECK(hv_vcpu_get_reg(vcpu, reg, &val));
    return val;
}

static inline void vcpu_set_reg(hv_vcpu_t vcpu, hv_reg_t reg, uint64_t val)
{
    HV_CHECK(hv_vcpu_set_reg(vcpu, reg, val));
}

static inline uint64_t vcpu_get_sysreg(hv_vcpu_t vcpu, hv_sys_reg_t reg)
{
    uint64_t val;
    HV_CHECK(hv_vcpu_get_sys_reg(vcpu, reg, &val));
    return val;
}

static inline void vcpu_set_sysreg(hv_vcpu_t vcpu,
                                   hv_sys_reg_t reg,
                                   uint64_t val)
{
    HV_CHECK(hv_vcpu_set_sys_reg(vcpu, reg, val));
}

/* Snapshot all 31 GPRs (X0..X30) into out[31].
 * Must be called on the owning thread.
 */
static inline void vcpu_snapshot_gprs(hv_vcpu_t vcpu, uint64_t out[31])
{
    for (unsigned i = 0; i < 31; i++)
        out[i] = vcpu_get_gpr(vcpu, i);
}

/* Restore all 31 GPRs (X0..X30) from in[31].
 * Must be called on the owning thread.
 */
static inline void vcpu_restore_gprs(hv_vcpu_t vcpu, const uint64_t in[31])
{
    for (unsigned i = 0; i < 31; i++)
        vcpu_set_gpr(vcpu, i, in[i]);
}

/* Zero all 31 GPRs. Used when entering EL0 with a clean register file. */
static inline void vcpu_zero_gprs(hv_vcpu_t vcpu)
{
    for (unsigned i = 0; i < 31; i++)
        vcpu_set_gpr(vcpu, i, 0);
}

static inline hv_simd_fp_uchar16_t vcpu_get_simd(hv_vcpu_t vcpu, unsigned n)
{
    hv_simd_fp_uchar16_t val;
    HV_CHECK(hv_vcpu_get_simd_fp_reg(vcpu, HV_SIMD_FP_REG_Q0 + n, &val));
    return val;
}

static inline void vcpu_set_simd(hv_vcpu_t vcpu,
                                 unsigned n,
                                 hv_simd_fp_uchar16_t val)
{
    HV_CHECK(hv_vcpu_set_simd_fp_reg(vcpu, HV_SIMD_FP_REG_Q0 + n, val));
}

/* SIMD/FP state snapshot.
 *
 * Bundles V0-V31 + FPSR + FPCR into a single struct for capture/restore.
 * Used by clone(CLONE_THREAD), clone(CLONE_VM), and IPC fork paths.
 */

typedef struct {
    hv_simd_fp_uchar16_t v[32];
    uint64_t fpsr, fpcr;
} vcpu_simd_state_t;

/* Snapshot all SIMD/FP state from a vCPU. Must be called on the owning
 * thread.
 */
static inline void vcpu_snapshot_simd(hv_vcpu_t vcpu, vcpu_simd_state_t *s)
{
    for (int i = 0; i < 32; i++)
        s->v[i] = vcpu_get_simd(vcpu, (unsigned) i);
    s->fpsr = vcpu_get_reg(vcpu, HV_REG_FPSR);
    s->fpcr = vcpu_get_reg(vcpu, HV_REG_FPCR);
}

/* Restore all SIMD/FP state to a vCPU. Must be called on the owning thread. */
static inline void vcpu_restore_simd(hv_vcpu_t vcpu, const vcpu_simd_state_t *s)
{
    for (int i = 0; i < 32; i++)
        vcpu_set_simd(vcpu, (unsigned) i, s->v[i]);
    vcpu_set_reg(vcpu, HV_REG_FPSR, s->fpsr);
    vcpu_set_reg(vcpu, HV_REG_FPCR, s->fpcr);
}
