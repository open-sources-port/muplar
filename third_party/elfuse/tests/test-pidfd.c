/* Test pidfd subsystem
 *
 * Copyright 2026 elfuse contributors
 * SPDX-License-Identifier: Apache-2.0
 *
 * Tests: pidfd_open, pidfd_send_signal, clone3 with CLONE_PIDFD,
 * and poll on pidfd for exit notification.
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>

#include "test-harness.h"
#include "raw-syscall.h"

#define __NR_getpid 172
#define __NR_pidfd_open 434
#define __NR_pidfd_send_signal 424
#define __NR_pidfd_getfd 438
#define __NR_close 57
#define __NR_clone3 435
#define __NR_ppoll 73
#define __NR_exit 93
#define __NR_wait4 260

#define CLONE_PIDFD 0x00001000
#define SIGCHLD 17

int passes = 0, fails = 0;

/* Linux clone3 args */
struct clone_args {
    uint64_t flags;
    uint64_t pidfd, child_tid, parent_tid, exit_signal, stack, stack_size, tls;
};

/* Linux pollfd (aarch64) */
struct linux_pollfd {
    int fd;
    short events, revents;
};

int main(void)
{
    printf("test-pidfd: pidfd subsystem\n");

    long pid = raw_syscall0(__NR_getpid);

    /* pidfd_open for self */
    TEST("pidfd_open(self, 0)");
    long pfd = raw_syscall2(__NR_pidfd_open, pid, 0);
    EXPECT_TRUE(pfd >= 0, "pidfd_open failed");

    /* pidfd_send_signal: signal 0 (validity check) */
    TEST("pidfd_send_signal(pfd, 0)");
    if (pfd >= 0) {
        long r = raw_syscall4(__NR_pidfd_send_signal, pfd, 0, 0, 0);
        EXPECT_TRUE(r == 0, "pidfd_send_signal sig=0 failed");
    } else {
        FAIL("no pidfd");
    }

    /* Close the self-pidfd */
    if (pfd >= 0)
        raw_syscall1(__NR_close, pfd);

    /* pidfd_open for unknown PID returns ESRCH */
    TEST("pidfd_open(99999) returns ESRCH");
    {
        long r = raw_syscall2(__NR_pidfd_open, 99999, 0);
        EXPECT_TRUE(r == -3 /* LINUX_ESRCH */, "expected -ESRCH");
        if (r >= 0)
            raw_syscall1(__NR_close, r);
    }

    /* pidfd_open with invalid flags */
    TEST("pidfd_open(self, 1) returns EINVAL");
    {
        long r = raw_syscall2(__NR_pidfd_open, pid, 1);
        EXPECT_TRUE(r == -22 /* LINUX_EINVAL */, "expected -EINVAL");
        if (r >= 0)
            raw_syscall1(__NR_close, r);
    }

    /* pidfd_getfd returns ENOSYS (not implemented) */
    TEST("pidfd_getfd returns ENOSYS");
    {
        long r = raw_syscall3(__NR_pidfd_getfd, 0, 0, 0);
        EXPECT_TRUE(r == -38 /* LINUX_ENOSYS */, "expected -ENOSYS");
    }

    /* clone3 with CLONE_PIDFD */
    TEST("clone3 CLONE_PIDFD");
    {
        /* Allocate stack for child */
        void *stack = mmap(NULL, 65536, PROT_READ | PROT_WRITE,
                           MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (stack == MAP_FAILED) {
            FAIL("mmap for stack failed");
            goto done;
        }

        int32_t child_pidfd = -1;
        struct clone_args ca = {0};
        ca.flags = CLONE_PIDFD;
        ca.pidfd = (uint64_t) &child_pidfd;
        ca.exit_signal = SIGCHLD;
        ca.stack = (uint64_t) stack;
        ca.stack_size = 65536;

        long child = raw_syscall2(__NR_clone3, (long) &ca, sizeof(ca));
        if (child == 0) {
            /* Child: exit immediately */
            raw_syscall1(__NR_exit, 42);
            __builtin_unreachable();
        }
        if (child < 0) {
            FAIL("clone3 failed");
            goto done;
        }

        EXPECT_TRUE(child_pidfd >= 0, "pidfd not written");

        /* Poll the pidfd for child exit (with 1s timeout) */
        TEST("poll pidfd for exit");
        if (child_pidfd >= 0) {
            struct linux_pollfd pf = {
                .fd = child_pidfd,
                .events = 1, /* POLLIN */
                .revents = 0,
            };
            /* 100ms timeout */
            uint64_t ts[2] = {0, 100000000};
            long pr = raw_syscall5(__NR_ppoll, (long) &pf, 1, (long) ts, 0, 0);
            EXPECT_TRUE(pr > 0 && (pf.revents & 1),
                        "pidfd not readable after child exit");

            raw_syscall1(__NR_close, child_pidfd);
        } else {
            FAIL("no pidfd to poll");
        }

        /* Reap the child */
        raw_syscall4(__NR_wait4, child, 0, 0, 0);
    }

done:
    SUMMARY("test-pidfd");
    return fails ? 1 : 0;
}
