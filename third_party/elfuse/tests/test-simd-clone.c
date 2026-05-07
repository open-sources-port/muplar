/* Verify SIMD/FP state preservation across
 * clone(CLONE_THREAD)
 *
 * Copyright 2026 elfuse contributors
 * Copyright 2025 Moritz Angermann, zw3rk pte. ltd.
 * SPDX-License-Identifier: Apache-2.0
 *
 * Loads known patterns into V0-V3, FPSR, and FPCR before clone.
 * The child thread reads them back and verifies the values match.
 * This catches the bug where child threads inherit zeroed SIMD state.
 */

#include <stdint.h>
#include <string.h>

#include "test-harness.h"
#include "raw-syscall.h"

int passes = 0, fails = 0;

/* 128-bit value for comparison */
typedef struct {
    uint64_t lo, hi;
} u128_t;

/* Shared results written by child */
static volatile u128_t child_v0, child_v1, child_v2, child_v3;
static volatile uint64_t child_fpcr, child_fpsr;
static volatile int child_done = 0;

static char child_stack_buf[16384] __attribute__((aligned(16)));

static void child_read_simd(void)
{
    u128_t v0, v1, v2, v3;
    uint64_t fpcr, fpsr;

    /* Single asm block: read all SIMD state atomically w.r.t. compiler
     * scheduling. Without this, the compiler could insert code between
     * separate asm volatile blocks that clobbers v0-v3.
     */
    __asm__ volatile(
        "mov %[v0lo], v0.d[0]\n\t"
        "mov %[v0hi], v0.d[1]\n\t"
        "mov %[v1lo], v1.d[0]\n\t"
        "mov %[v1hi], v1.d[1]\n\t"
        "mov %[v2lo], v2.d[0]\n\t"
        "mov %[v2hi], v2.d[1]\n\t"
        "mov %[v3lo], v3.d[0]\n\t"
        "mov %[v3hi], v3.d[1]\n\t"
        "mrs %[fc], fpcr\n\t"
        "mrs %[fs], fpsr\n\t"
        : [v0lo] "=r"(v0.lo), [v0hi] "=r"(v0.hi), [v1lo] "=r"(v1.lo),
          [v1hi] "=r"(v1.hi), [v2lo] "=r"(v2.lo), [v2hi] "=r"(v2.hi),
          [v3lo] "=r"(v3.lo), [v3hi] "=r"(v3.hi), [fc] "=r"(fpcr),
          [fs] "=r"(fpsr));

    child_v0 = v0;
    child_v1 = v1;
    child_v2 = v2;
    child_v3 = v3;
    child_fpcr = fpcr;
    child_fpsr = fpsr;

    child_done = 1;
    raw_futex_wake((int *) &child_done, 1);
    raw_exit(0);
}

static void test_simd_preserved(void)
{
    TEST("SIMD regs across clone");

    child_done = 0;

    /* Load known patterns into V0-V3 and FPCR */
    uint64_t pat0_lo = 0xDEADBEEFCAFEBABEULL, pat0_hi = 0x0123456789ABCDEFULL;
    uint64_t pat1_lo = 0x1111111111111111ULL, pat1_hi = 0x2222222222222222ULL;
    uint64_t pat2_lo = 0xAAAAAAAAAAAAAAAAULL, pat2_hi = 0x5555555555555555ULL;
    uint64_t pat3_lo = 0xFFFFFFFFFFFFFFFFULL, pat3_hi = 0x8000000000000001ULL;

    /* FPCR: set RMode to round-toward-zero (bits 23:22 = 0b11) */
    uint64_t fpcr_val = (3ULL << 22);
    /* FPSR: set IOC sticky bit (bit 0), a legal nonzero FPSR value */
    uint64_t fpsr_val = 1ULL;

    __asm__ volatile(
        "msr fpcr, %[fc]\n\t"
        "msr fpsr, %[fs]\n\t"
        "mov v0.d[0], %[p0l]\n\t"
        "mov v0.d[1], %[p0h]\n\t"
        "mov v1.d[0], %[p1l]\n\t"
        "mov v1.d[1], %[p1h]\n\t"
        "mov v2.d[0], %[p2l]\n\t"
        "mov v2.d[1], %[p2h]\n\t"
        "mov v3.d[0], %[p3l]\n\t"
        "mov v3.d[1], %[p3h]\n\t"
        :
        : [fc] "r"(fpcr_val), [fs] "r"(fpsr_val), [p0l] "r"(pat0_lo),
          [p0h] "r"(pat0_hi), [p1l] "r"(pat1_lo), [p1h] "r"(pat1_hi),
          [p2l] "r"(pat2_lo), [p2h] "r"(pat2_hi), [p3l] "r"(pat3_lo),
          [p3h] "r"(pat3_hi)
        : "v0", "v1", "v2", "v3");

    /* Clone with CLONE_THREAD */
    unsigned long flags = 0x7d0f00; /* CLONE_VM|FS|FILES|SIGHAND|THREAD|... */
    volatile int ctid = 0;
    void *stack_top = child_stack_buf + sizeof(child_stack_buf);

    long ret = raw_clone(flags, stack_top, (int *) &ctid, 0, (int *) &ctid);

    if (ret == 0) {
        child_read_simd();
        __builtin_unreachable();
    }

    if (ret < 0) {
        FAIL("clone failed");
        return;
    }

    /* Wait for child */
    while (child_done == 0)
        raw_futex_wait((int *) &child_done, 0);

    /* Verify */
    int ok = 1;
    if (child_v0.lo != pat0_lo || child_v0.hi != pat0_hi) {
        printf("V0 mismatch: got 0x%llx:%llx, want 0x%llx:%llx\n",
               (unsigned long long) child_v0.hi,
               (unsigned long long) child_v0.lo, (unsigned long long) pat0_hi,
               (unsigned long long) pat0_lo);
        ok = 0;
    }
    if (child_v1.lo != pat1_lo || child_v1.hi != pat1_hi) {
        printf("V1 mismatch\n");
        ok = 0;
    }
    if (child_v2.lo != pat2_lo || child_v2.hi != pat2_hi) {
        printf("V2 mismatch\n");
        ok = 0;
    }
    if (child_v3.lo != pat3_lo || child_v3.hi != pat3_hi) {
        printf("V3 mismatch\n");
        ok = 0;
    }
    if (child_fpsr != fpsr_val) {
        printf("FPSR mismatch: got 0x%llx, want 0x%llx\n",
               (unsigned long long) child_fpsr, (unsigned long long) fpsr_val);
        ok = 0;
    }
    if (child_fpcr != fpcr_val) {
        printf("FPCR mismatch: got 0x%llx, want 0x%llx\n",
               (unsigned long long) child_fpcr, (unsigned long long) fpcr_val);
        ok = 0;
    }

    /* Wait for child exit (CLEARTID) */
    while (ctid != 0)
        raw_futex_wait((int *) &ctid, ctid);

    EXPECT_TRUE(ok, "SIMD state not preserved");
}

int main(void)
{
    printf("test-simd-clone: SIMD/FP state preservation across clone\n");
    test_simd_preserved();
    SUMMARY("test-simd-clone");
    return fails > 0 ? 1 : 0;
}
