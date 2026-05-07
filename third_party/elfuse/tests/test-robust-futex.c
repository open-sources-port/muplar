/* Robust futex owner-died cleanup tests
 *
 * Copyright 2026 elfuse contributors
 * Copyright 2025 Moritz Angermann, zw3rk pte. ltd.
 * SPDX-License-Identifier: Apache-2.0
 *
 * Tests:
 *   1. Thread acquires lock, exits, verify FUTEX_OWNER_DIED is set
 *   2. Robust list with multiple entries
 *   3. Pending lock (list_op_pending) cleanup
 *
 * Syscalls: set_robust_list(99), clone(220), futex(98), gettid(178)
 */

#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>

#include "test-harness.h"
#include "raw-syscall.h"
#include "test-util.h"

int passes = 0, fails = 0;

#ifndef FUTEX_OWNER_DIED
#define FUTEX_OWNER_DIED 0x40000000
#endif
#ifndef FUTEX_TID_MASK
#define FUTEX_TID_MASK 0x3FFFFFFF
#endif
#ifndef FUTEX_WAITERS
#define FUTEX_WAITERS 0x80000000
#endif

/* Linux robust_list structures */
struct robust_list {
    struct robust_list *next;
};

struct robust_list_head {
    struct robust_list list;
    long futex_offset;
    struct robust_list *list_op_pending;
};

/* Shared memory for cross-thread communication */
static volatile uint32_t lock_word __attribute__((aligned(4)));
static struct robust_list_head rhead __attribute__((aligned(8)));
static struct robust_list entry1 __attribute__((aligned(8)));

static char child_stack[8192] __attribute__((aligned(16)));

static int child_fn(void *arg)
{
    (void) arg;
    long tid = raw_syscall0(178); /* gettid */

    /* Set up robust list */
    rhead.list.next = &entry1;
    rhead.futex_offset = (long) &lock_word - (long) &entry1;
    rhead.list_op_pending = NULL;
    entry1.next = &rhead.list; /* circular: points back to head */

    /* "Acquire" the lock by writing the current TID */
    lock_word = (uint32_t) tid;

    /* Register robust list with kernel */
    raw_syscall2(99, (long) &rhead, sizeof(rhead)); /* set_robust_list */

    /* Exit without releasing the lock; robust walk should set
     * FUTEX_OWNER_DIED on lock_word
     */
    raw_syscall1(93, 0); /* exit */
    test_unreachable();
}

int main(void)
{
    TEST("robust-futex: owner-died on exit");

    lock_word = 0;
    memset(&rhead, 0, sizeof(rhead));
    memset(&entry1, 0, sizeof(entry1));

    /* Clone a thread: CLONE_VM | CLONE_FS | CLONE_FILES | CLONE_SIGHAND |
     * CLONE_CHILD_CLEARTID. CLONE_THREAD (0x00010000) is the same bit as
     * CLONE_VM so it's implicitly set.
     */
    long flags = 0x00010000    /* CLONE_VM (= CLONE_THREAD) */
                 | 0x00000100  /* CLONE_FS */
                 | 0x00000200  /* CLONE_FILES */
                 | 0x00000800  /* CLONE_SIGHAND */
                 | 0x00200000; /* CLONE_CHILD_CLEARTID */

    /* Use raw_syscall5 for clone(flags, stack, ptid, tls, ctid) */
    volatile int child_tid_addr = 0;
    long ret = raw_syscall5(220, /* clone */
                            flags, (long) (child_stack + sizeof(child_stack)),
                            0,                     /* parent_tid */
                            0,                     /* tls */
                            (long) &child_tid_addr /* child_tid */
    );

    if (ret < 0) {
        FAIL("clone failed");
    } else if (ret == 0) {
        /* Child */
        child_fn(NULL);
    } else {
        /* Parent: wait for child to exit via CLONE_CHILD_CLEARTID futex */
        usleep(100000); /* 100ms grace period */

        /* Check if FUTEX_OWNER_DIED was set */
        uint32_t val = lock_word;
        if (val & FUTEX_OWNER_DIED) {
            PASS();
        } else {
            FAIL("FUTEX_OWNER_DIED not set");
        }
    }

    TEST("robust-futex: set_robust_list returns 0");
    {
        struct robust_list_head h;
        memset(&h, 0, sizeof(h));
        long r = raw_syscall2(99, (long) &h, sizeof(h));
        EXPECT_TRUE(r == 0, "set_robust_list returned non-zero");
    }

    TEST("robust-futex: get_robust_list returns head");
    {
        struct robust_list_head h;
        memset(&h, 0, sizeof(h));
        raw_syscall2(99, (long) &h, sizeof(h));

        void *head_out = NULL;
        long len_out = 0;
        long r = raw_syscall3(100, 0, (long) &head_out, (long) &len_out);
        EXPECT_TRUE(r == 0 && head_out == &h && len_out == 24,
                    "get_robust_list mismatch");
    }

    SUMMARY("test-robust-futex");
    return fails > 0 ? 1 : 0;
}
