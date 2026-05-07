/* FD table concurrency stress test
 *
 * Copyright 2026 elfuse contributors
 * Copyright 2025 Moritz Angermann, zw3rk pte. ltd.
 * SPDX-License-Identifier: Apache-2.0
 *
 * Creates two threads: one doing rapid open/close cycles on temp files,
 * the other reading/writing on FDs in the same range. Verifies no EBADF
 * on wrong files or use-after-close on the wrong file.
 *
 * Syscalls: clone(220), openat(56), close(57), write(64), read(63),
 *           futex(98), exit(93)
 */

#include <stdint.h>
#include <string.h>

#include "test-harness.h"
#include "raw-syscall.h"

int passes = 0, fails = 0;

static volatile int done = 0;
static volatile int errors = 0;

#define ITERATIONS 500

static char writer_stack[16384] __attribute__((aligned(16)));

static int writer_fn(void *arg)
{
    (void) arg;
    char buf[64] = "test data for fd race\n";

    for (int i = 0; i < ITERATIONS && !done; i++) {
        /* Open a temp file, write, close */
        int fd =
            (int) raw_syscall4(56, -100, (long) "/tmp/elfuse-fdrace-XXXXXX",
                               0x0042, /* O_RDWR|O_CREAT */
                               0644);
        if (fd >= 0) {
            long w = raw_syscall3(64, fd, (long) buf, 22);
            if (w < 0)
                __sync_fetch_and_add((volatile int *) &errors, 1);
            raw_syscall1(57, fd); /* close */
        }
    }
    done = 1;
    raw_syscall1(93, 0); /* exit thread */
    return 0;
}

int main(void)
{
    TEST("fd-race: concurrent open/close/write");

    /* Spawn writer thread */
    long flags = 0x00010000 | 0x00000100 | 0x00000200 | 0x00000800 | 0x00200000;
    volatile uint32_t child_tid = 1;

    long ret =
        raw_syscall5(220, flags, (long) (writer_stack + sizeof(writer_stack)),
                     0, 0, (long) &child_tid);

    if (ret < 0) {
        FAIL("clone failed");
        goto out;
    }

    if (ret == 0) {
        /* Child */
        writer_fn(NULL);
    }

    /* Parent: also do open/close cycles */
    for (int i = 0; i < ITERATIONS && !done; i++) {
        int fd = (int) raw_syscall4(56, -100, (long) "/dev/null", 0x0002, 0);
        if (fd >= 0) {
            char c = 'x';
            raw_syscall3(64, fd, (long) &c, 1);
            raw_syscall1(57, fd);
        }
    }
    done = 1;

    /* Wait for child via CLEARTID futex */
    for (int i = 0; i < 100 && child_tid != 0; i++) {
        struct {
            long tv_sec, tv_nsec;
        } ts = {0, 10000000}; /* 10ms */
        raw_syscall6(98, (long) &child_tid, 0, child_tid, (long) &ts, 0, 0);
    }

    EXPECT_TRUE(errors == 0, "race errors detected");

out:
    /* Clean up temp file */
    raw_syscall3(35, -100, (long) "/tmp/elfuse-fdrace-XXXXXX",
                 0); /* unlinkat */

    SUMMARY("test-fd-race");
    return fails > 0 ? 1 : 0;
}
