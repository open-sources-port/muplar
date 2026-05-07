/* Test signalfd4 syscall emulation
 *
 * Copyright 2026 elfuse contributors
 * Copyright 2025 Moritz Angermann, zw3rk pte. ltd.
 * SPDX-License-Identifier: Apache-2.0
 *
 * Tests: signalfd create with flags, read signalfd_siginfo after kill(),
 *        verify ssi_signo/ssi_pid fields, EAGAIN when no signal pending
 *
 * Syscalls exercised: signalfd4(74), rt_sigprocmask(135), kill(129),
 *                     read(63), close(57)
 */

#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/signalfd.h>

#include "test-harness.h"

int main(void)
{
    int passes = 0, fails = 0;

    printf("test-signalfd: signalfd4 emulation tests\n");

    /* Block SIGUSR1 so it stays pending for signalfd delivery */
    sigset_t mask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGUSR1);
    sigprocmask(SIG_BLOCK, &mask, NULL);

    /* Test signalfd create with flags */
    TEST("signalfd create");
    {
        int fd = signalfd(-1, &mask, SFD_NONBLOCK | SFD_CLOEXEC);
        EXPECT_TRUE(fd >= 0, "signalfd create failed");
        close(fd);
    }

    /* Test: send signal, then read signalfd_siginfo */
    TEST("kill + read siginfo");
    {
        int fd = signalfd(-1, &mask, SFD_NONBLOCK);
        if (fd >= 0) {
            kill(getpid(), SIGUSR1);
            struct signalfd_siginfo info;
            memset(&info, 0, sizeof(info));
            ssize_t r = read(fd, &info, sizeof(info));
            if (r == (ssize_t) sizeof(info)) {
                EXPECT_TRUE(info.ssi_signo == (uint32_t) SIGUSR1,
                            "wrong signo");
            } else {
                FAIL("read failed or short");
            }
            close(fd);
        } else
            FAIL("signalfd create failed");
    }

    /* Test: verify ssi_pid matches getpid() */
    TEST("ssi_pid == getpid()");
    {
        int fd = signalfd(-1, &mask, SFD_NONBLOCK);
        if (fd >= 0) {
            kill(getpid(), SIGUSR1);
            struct signalfd_siginfo info;
            memset(&info, 0, sizeof(info));
            ssize_t r = read(fd, &info, sizeof(info));
            if (r == (ssize_t) sizeof(info)) {
                EXPECT_TRUE(info.ssi_pid == (uint32_t) getpid(), "wrong pid");
            } else {
                FAIL("read failed");
            }
            close(fd);
        } else
            FAIL("signalfd create failed");
    }

    /* Test: EAGAIN when no signal is pending */
    TEST("EAGAIN when empty");
    {
        /* Use SIGUSR2 which the test has not sent */
        sigset_t mask2;
        sigemptyset(&mask2);
        sigaddset(&mask2, SIGUSR2);
        sigprocmask(SIG_BLOCK, &mask2, NULL);

        int fd = signalfd(-1, &mask2, SFD_NONBLOCK);
        if (fd >= 0) {
            struct signalfd_siginfo info;
            EXPECT_ERRNO(read(fd, &info, sizeof(info)), EAGAIN,
                         "expected EAGAIN");
            close(fd);
        } else
            FAIL("signalfd create failed");
    }

    TEST("signalfd invalid flags");
    EXPECT_ERRNO(signalfd(-1, &mask, 0x40000000), EINVAL, "expected EINVAL");

    SUMMARY("test-signalfd");
    return fails > 0 ? 1 : 0;
}
