/* futex_waitv (SYS 449) regression
 *
 * Copyright 2026 elfuse contributors
 * SPDX-License-Identifier: Apache-2.0
 *
 * Tests:
 *   1. Single waiter wakes when its futex is FUTEX_WAKEd; returns index 0.
 *   2. Multi waiter: a wake on element[N] returns N, not 0 or another index.
 *   3. EAGAIN when any element's val mismatches the in-memory word.
 *   4. EINVAL on nr_futexes=0, nr_futexes>128, top-level flags!=0,
 *      element flags with reserved bits, element size != FUTEX2_SIZE_U32,
 *      and clockid not in {CLOCK_REALTIME, CLOCK_MONOTONIC} with non-NULL
 *      timeout.
 *   5. ETIMEDOUT when no waker arrives before an absolute CLOCK_MONOTONIC
 *      deadline.
 *   6. ETIMEDOUT under CLOCK_REALTIME deadline (selected via the 5th syscall
 *      argument). Verifies that the dispatch wrapper actually forwards x4.
 *
 * Syscalls exercised: futex_waitv(449), futex(98), nanosleep(101).
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <pthread.h>
#include <unistd.h>
#include <sys/mman.h>
#include <linux/futex.h>

#include "test-harness.h"
#include "raw-syscall.h"

#ifndef __NR_futex_waitv
#define __NR_futex_waitv 449
#endif

#define FUTEX_WAITV_MAX 128
#define FUTEX2_SIZE_U32 0x02
#define FUTEX2_PRIVATE 0x80

#ifndef CLOCK_REALTIME
#define CLOCK_REALTIME 0
#endif
#ifndef CLOCK_MONOTONIC
#define CLOCK_MONOTONIC 1
#endif

struct waitv_elem {
    uint64_t val;
    uint64_t uaddr;
    uint32_t flags;
    uint32_t __reserved;
};

int passes = 0, fails = 0;

static long raw_futex_waitv(struct waitv_elem *waiters,
                            unsigned int nr,
                            unsigned int flags,
                            struct timespec *timeout,
                            int clockid)
{
    return raw_syscall5(__NR_futex_waitv, (long) waiters, (long) nr,
                        (long) flags, (long) timeout, (long) clockid);
}

/* Helper: wake @addr after @sleep_ms milliseconds. Used to unblock the main
 * thread's futex_waitv. The thread is joined by the test before the test
 * returns, so the wake completes before any state on the test's stack goes
 * out of scope.
 */
struct waker_args {
    uint32_t *addr;
    long sleep_ms;
};

static void *waker_thread(void *arg)
{
    struct waker_args *a = (struct waker_args *) arg;
    struct timespec ts = {.tv_sec = a->sleep_ms / 1000,
                          .tv_nsec = (a->sleep_ms % 1000) * 1000000L};
    nanosleep(&ts, NULL);
    raw_futex_wake((int *) a->addr, 1);
    return NULL;
}

static void test_single_wake(void)
{
    TEST("single waiter wakes index 0");

    uint32_t f __attribute__((aligned(4))) = 0;
    struct waitv_elem w = {
        .val = 0,
        .uaddr = (uint64_t) (uintptr_t) &f,
        .flags = FUTEX2_SIZE_U32 | FUTEX2_PRIVATE,
    };

    pthread_t tid;
    struct waker_args wa = {.addr = &f, .sleep_ms = 50};
    pthread_create(&tid, NULL, waker_thread, &wa);

    long r = raw_futex_waitv(&w, 1, 0, NULL, CLOCK_MONOTONIC);
    pthread_join(tid, NULL);

    EXPECT_EQ(r, 0, "expected index 0");
}

static void test_multi_wake_index(void)
{
    TEST("multi waiter returns woken index");

    uint32_t f[4] __attribute__((aligned(4))) = {0, 0, 0, 0};
    struct waitv_elem ws[4];
    for (int i = 0; i < 4; i++) {
        ws[i].val = 0;
        ws[i].uaddr = (uint64_t) (uintptr_t) &f[i];
        ws[i].flags = FUTEX2_SIZE_U32 | FUTEX2_PRIVATE;
        ws[i].__reserved = 0;
    }

    pthread_t tid;
    struct waker_args wa = {.addr = &f[2], .sleep_ms = 50};
    pthread_create(&tid, NULL, waker_thread, &wa);

    long r = raw_futex_waitv(ws, 4, 0, NULL, CLOCK_MONOTONIC);
    pthread_join(tid, NULL);

    EXPECT_EQ(r, 2, "expected index 2");
}

static void test_eagain_stale(void)
{
    TEST("EAGAIN on stale value");

    uint32_t f[2] __attribute__((aligned(4))) = {0, 7};
    struct waitv_elem ws[2] = {
        {.val = 0,
         .uaddr = (uint64_t) (uintptr_t) &f[0],
         .flags = FUTEX2_SIZE_U32 | FUTEX2_PRIVATE},
        /* second element has a stale expected value; futex_waitv should fail
         * the whole batch with EAGAIN before blocking.
         */
        {.val = 99,
         .uaddr = (uint64_t) (uintptr_t) &f[1],
         .flags = FUTEX2_SIZE_U32 | FUTEX2_PRIVATE},
    };

    long r = raw_futex_waitv(ws, 2, 0, NULL, CLOCK_MONOTONIC);
    EXPECT_RAW_ERRNO(r, -11 /* -EAGAIN */, "expected -EAGAIN");
}

static void test_einval_paths(void)
{
    uint32_t f __attribute__((aligned(4))) = 0;
    struct waitv_elem w = {
        .val = 0,
        .uaddr = (uint64_t) (uintptr_t) &f,
        .flags = FUTEX2_SIZE_U32 | FUTEX2_PRIVATE,
    };

    TEST("EINVAL nr_futexes=0");
    long r = raw_futex_waitv(&w, 0, 0, NULL, CLOCK_MONOTONIC);
    EXPECT_RAW_ERRNO(r, -22, "nr=0");

    TEST("EINVAL nr_futexes>128");
    r = raw_futex_waitv(&w, FUTEX_WAITV_MAX + 1, 0, NULL, CLOCK_MONOTONIC);
    EXPECT_RAW_ERRNO(r, -22, "nr>128");

    TEST("EINVAL top-level flags!=0");
    r = raw_futex_waitv(&w, 1, 1, NULL, CLOCK_MONOTONIC);
    EXPECT_RAW_ERRNO(r, -22, "flags!=0");

    TEST("EINVAL reserved!=0");
    {
        struct waitv_elem bad = w;
        bad.__reserved = 1;
        r = raw_futex_waitv(&bad, 1, 0, NULL, CLOCK_MONOTONIC);
        EXPECT_RAW_ERRNO(r, -22, "reserved!=0");
    }

    TEST("EINVAL element flags reserved bits");
    {
        struct waitv_elem bad = w;
        bad.flags = FUTEX2_SIZE_U32 | 0x100; /* bit 8 is reserved */
        r = raw_futex_waitv(&bad, 1, 0, NULL, CLOCK_MONOTONIC);
        EXPECT_RAW_ERRNO(r, -22, "elt-flags reserved");
    }

    TEST("EINVAL element size != U32");
    {
        struct waitv_elem bad = w;
        bad.flags = 0x01 /* SIZE_U16 */ | FUTEX2_PRIVATE;
        r = raw_futex_waitv(&bad, 1, 0, NULL, CLOCK_MONOTONIC);
        EXPECT_RAW_ERRNO(r, -22, "size!=U32");
    }

    TEST("EINVAL bad clockid with timeout");
    {
        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        ts.tv_sec += 1;
        r = raw_futex_waitv(&w, 1, 0, &ts, 7 /* CLOCK_BOOTTIME */);
        EXPECT_RAW_ERRNO(r, -22, "bad clockid");
    }

    TEST("EINVAL bad CLOCK_REALTIME deadline nsec");
    {
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        ts.tv_nsec = 1000000000L;
        r = raw_futex_waitv(&w, 1, 0, &ts, CLOCK_REALTIME);
        EXPECT_RAW_ERRNO(r, -22, "bad realtime timeout");
    }

    TEST("EINVAL bad CLOCK_MONOTONIC deadline nsec");
    {
        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        ts.tv_nsec = 1000000000L;
        r = raw_futex_waitv(&w, 1, 0, &ts, CLOCK_MONOTONIC);
        EXPECT_RAW_ERRNO(r, -22, "bad monotonic timeout");
    }
}

static void test_timeout_monotonic(void)
{
    TEST("ETIMEDOUT CLOCK_MONOTONIC deadline");

    uint32_t f __attribute__((aligned(4))) = 0;
    struct waitv_elem w = {
        .val = 0,
        .uaddr = (uint64_t) (uintptr_t) &f,
        .flags = FUTEX2_SIZE_U32 | FUTEX2_PRIVATE,
    };

    struct timespec deadline;
    clock_gettime(CLOCK_MONOTONIC, &deadline);
    deadline.tv_nsec += 100 * 1000000L; /* +100 ms */
    if (deadline.tv_nsec >= 1000000000L) {
        deadline.tv_sec += 1;
        deadline.tv_nsec -= 1000000000L;
    }

    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    long r = raw_futex_waitv(&w, 1, 0, &deadline, CLOCK_MONOTONIC);
    clock_gettime(CLOCK_MONOTONIC, &t1);

    long elapsed_ms =
        (t1.tv_sec - t0.tv_sec) * 1000 + (t1.tv_nsec - t0.tv_nsec) / 1000000L;
    if (r == -110 /* -ETIMEDOUT */ && elapsed_ms >= 50 && elapsed_ms <= 2000) {
        PASS();
    } else {
        printf("FAIL: r=%ld elapsed=%ldms (expected -110, 50<=elapsed<=2000)\n",
               r, elapsed_ms);
        fails++;
    }
}

static void test_timeout_realtime(void)
{
    TEST("ETIMEDOUT CLOCK_REALTIME deadline");

    uint32_t f __attribute__((aligned(4))) = 0;
    struct waitv_elem w = {
        .val = 0,
        .uaddr = (uint64_t) (uintptr_t) &f,
        .flags = FUTEX2_SIZE_U32 | FUTEX2_PRIVATE,
    };

    struct timespec deadline;
    clock_gettime(CLOCK_REALTIME, &deadline);
    deadline.tv_nsec += 100 * 1000000L; /* +100 ms */
    if (deadline.tv_nsec >= 1000000000L) {
        deadline.tv_sec += 1;
        deadline.tv_nsec -= 1000000000L;
    }

    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    long r = raw_futex_waitv(&w, 1, 0, &deadline, CLOCK_REALTIME);
    clock_gettime(CLOCK_MONOTONIC, &t1);

    long elapsed_ms =
        (t1.tv_sec - t0.tv_sec) * 1000 + (t1.tv_nsec - t0.tv_nsec) / 1000000L;
    if (r == -110 && elapsed_ms >= 50 && elapsed_ms <= 2000) {
        PASS();
    } else {
        printf("FAIL: r=%ld elapsed=%ldms (expected -110, 50<=elapsed<=2000)\n",
               r, elapsed_ms);
        fails++;
    }
}

/* Reviewer-driven coverage: alignment, NULL pointers, exact 128, faults. */

static void test_einval_unaligned(void)
{
    TEST("EINVAL unaligned uaddr");

    /* Build a uaddr that is 1 byte off the natural 4-byte boundary. The
     * underlying storage is still inside a writable mapping, so this
     * exercises the alignment check rather than a fault path.
     */
    static uint8_t buf[16] __attribute__((aligned(4)));
    struct waitv_elem w = {
        .val = 0,
        .uaddr = (uint64_t) (uintptr_t) (buf + 1),
        .flags = FUTEX2_SIZE_U32 | FUTEX2_PRIVATE,
    };
    long r = raw_futex_waitv(&w, 1, 0, NULL, CLOCK_MONOTONIC);
    EXPECT_RAW_ERRNO(r, -22, "expected -EINVAL on unaligned uaddr");
}

static void test_einval_null_waiters(void)
{
    TEST("EINVAL NULL waiters pointer");

    /* The Linux kernel rejects waiters==NULL up-front with -EINVAL (the
     * !waiters branch in the !nr_futexes||nr_futexes>FUTEX_WAITV_MAX
     * predicate), not at copy_from_user time. Match that.
     */
    long r = raw_futex_waitv(NULL, 1, 0, NULL, CLOCK_MONOTONIC);
    EXPECT_RAW_ERRNO(r, -22 /* -EINVAL */, "expected -EINVAL on NULL waiters");
}

static void test_efault_timeout(void)
{
    TEST("EFAULT NULL-page timeout pointer");

    uint32_t f __attribute__((aligned(4))) = 0;
    struct waitv_elem w = {
        .val = 0,
        .uaddr = (uint64_t) (uintptr_t) &f,
        .flags = FUTEX2_SIZE_U32 | FUTEX2_PRIVATE,
    };

    /* Carve a PROT_NONE page so the timeout pointer is non-NULL but reads
     * fault at copy time.
     */
    void *p = mmap(NULL, 4096, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (p == MAP_FAILED) {
        FAIL("mmap PROT_NONE setup failed");
        return;
    }
    struct timespec *bad = (struct timespec *) p;
    long r = raw_futex_waitv(&w, 1, 0, bad, CLOCK_MONOTONIC);
    munmap(p, 4096);
    EXPECT_RAW_ERRNO(r, -14, "expected -EFAULT on faulting timeout");
}

static void test_efault_uaddr(void)
{
    TEST("EFAULT PROT_NONE uaddr at enqueue");

    /* Map a PROT_NONE page and aim a waiter at it. Linux returns EFAULT
     * when the kernel tries to read *uaddr to compare against val.
     */
    void *p = mmap(NULL, 4096, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (p == MAP_FAILED) {
        FAIL("mmap PROT_NONE setup failed");
        return;
    }
    struct waitv_elem w = {
        .val = 0,
        .uaddr = (uint64_t) (uintptr_t) p,
        .flags = FUTEX2_SIZE_U32 | FUTEX2_PRIVATE,
    };
    long r = raw_futex_waitv(&w, 1, 0, NULL, CLOCK_MONOTONIC);
    munmap(p, 4096);
    EXPECT_RAW_ERRNO(r, -14, "expected -EFAULT on PROT_NONE uaddr");
}

static void test_max_nr_128(void)
{
    TEST("nr_futexes==128 accepted (timeout path)");

    /* Allocate 128 4-byte slots in a single 4 KiB page so they all fit. The
     * call uses an immediate deadline (already-past timestamp) so it returns
     * ETIMEDOUT instead of blocking; this still pins down the inclusive 128
     * upper bound.
     */
    static uint32_t slots[128] __attribute__((aligned(4)));
    struct waitv_elem ws[128];
    for (int i = 0; i < 128; i++) {
        slots[i] = 0;
        ws[i].val = 0;
        ws[i].uaddr = (uint64_t) (uintptr_t) &slots[i];
        ws[i].flags = FUTEX2_SIZE_U32 | FUTEX2_PRIVATE;
        ws[i].__reserved = 0;
    }
    struct timespec deadline;
    clock_gettime(CLOCK_MONOTONIC, &deadline);
    /* Already-past deadline forces immediate ETIMEDOUT */
    if (deadline.tv_sec > 0)
        deadline.tv_sec -= 1;
    long r = raw_futex_waitv(ws, 128, 0, &deadline, CLOCK_MONOTONIC);
    EXPECT_RAW_ERRNO(r, -110, "expected -ETIMEDOUT (128 elements accepted)");
}

int main(void)
{
    printf("test-futex-waitv:\n");
    test_single_wake();
    test_multi_wake_index();
    test_eagain_stale();
    test_einval_paths();
    test_einval_unaligned();
    test_einval_null_waiters();
    test_efault_timeout();
    test_efault_uaddr();
    test_max_nr_128();
    test_timeout_monotonic();
    test_timeout_realtime();
    SUMMARY("test-futex-waitv");
    return fails == 0 ? 0 : 1;
}
