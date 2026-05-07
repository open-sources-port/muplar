/* Multithreaded fork snapshot consistency test
 *
 * Copyright 2026 elfuse contributors
 * Copyright 2025 Moritz Angermann, zw3rk pte. ltd.
 * SPDX-License-Identifier: Apache-2.0
 *
 * Creates N worker threads that write to shared memory, then forks
 * from the main thread. Verifies the child's snapshot is consistent
 * (no torn writes from partially-quiesced workers).
 *
 * Syscalls: clone(220), futex(98), clone/fork, exit(93), write(64)
 */

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "test-harness.h"
#include "raw-syscall.h"

int passes = 0, fails = 0;

#define N_WORKERS 3
#define PATTERN_SIZE 256

/* Shared memory region for workers to write patterns */
static volatile uint64_t shared_data[PATTERN_SIZE];
static volatile int workers_ready = 0;
static volatile int start_writing = 0;
static volatile int stop_writing = 0;

static char stacks[N_WORKERS][8192] __attribute__((aligned(16)));

static int worker_fn(void *arg)
{
    long id = (long) arg;
    uint64_t pattern = 0xDEAD0000ULL | (uint64_t) id;

    /* Signal ready */
    __sync_fetch_and_add(&workers_ready, 1);

    /* Wait for go signal */
    while (!start_writing)
        raw_syscall0(124); /* sched_yield */

    /* Write pattern repeatedly until told to stop */
    while (!stop_writing) {
        for (int i = 0; i < PATTERN_SIZE; i++)
            shared_data[i] = pattern;
    }

    raw_syscall1(93, 0); /* exit thread */
    return 0;
}

int main(void)
{
    TEST("mt-fork: workers quiesce for fork");

    memset((void *) shared_data, 0, sizeof(shared_data));

    /* Spawn workers */
    long flags = 0x00010000 | 0x00000100 | 0x00000200 | 0x00000800 | 0x00200000;

    for (int i = 0; i < N_WORKERS; i++) {
        long ret = raw_syscall5(
            220, flags, (long) (stacks[i] + sizeof(stacks[i])), 0, 0, 0);
        if (ret < 0) {
            FAIL("clone worker failed");
            goto out;
        }
        if (ret == 0) {
            worker_fn((void *) (long) i);
        }
    }

    /* Wait for all workers to be ready */
    while (workers_ready < N_WORKERS)
        raw_syscall0(124); /* sched_yield */

    /* Start writing */
    start_writing = 1;

    /* Let workers write for a bit */
    struct {
        long tv_sec, tv_nsec;
    } ts = {0, 50000000};                   /* 50ms */
    raw_syscall4(101, (long) &ts, 0, 0, 0); /* nanosleep */

    /* Fork - workers should be quiesced during snapshot */
    long child = raw_syscall5(220, 17, /* SIGCHLD (fork) */
                              0, 0, 0, 0);

    if (child < 0) {
        FAIL("fork failed");
        stop_writing = 1;
        goto out;
    }

    if (child == 0) {
        /* Child: check snapshot consistency. Workers write 8-byte
         * patterns (0xDEAD0000|id). On aarch64, aligned 8-byte stores
         * are atomic, so each entry must be a valid pattern (not a
         * torn mix of two patterns). Full-array consistency is NOT
         * guaranteed because fork snapshots memory between instructions,
         * and a worker may be mid-loop.
         */
        bool consistent = true;
        for (int i = 0; i < PATTERN_SIZE; i++) {
            uint64_t val = shared_data[i];
            if (val == 0)
                continue; /* not yet written */
            if ((val & 0xFFFF0000ULL) != 0xDEAD0000ULL) {
                consistent = false;
                break;
            }
            uint64_t id = val & 0xFFFF;
            if (id >= N_WORKERS) {
                consistent = false;
                break;
            }
        }
        /* Exit with consistency result */
        raw_syscall1(94, consistent ? 0 : 1); /* exit_group */
    }

    /* Parent: stop workers, wait for child */
    stop_writing = 1;

    /* Wait for fork child (wait4 with child PID) */
    int status = 0;
    raw_syscall4(260, (long) (int) child, (long) &status, 0, 0);

    if (status == 0) /* WIFEXITED(0) && WEXITSTATUS == 0 */
        PASS();
    else
        FAIL("child saw torn write");

out:
    stop_writing = 1;
    /* Brief delay for workers to exit */
    ts.tv_nsec = 50000000;
    raw_syscall4(101, (long) &ts, 0, 0, 0);

    SUMMARY("test-mt-fork");
    return fails > 0 ? 1 : 0;
}
