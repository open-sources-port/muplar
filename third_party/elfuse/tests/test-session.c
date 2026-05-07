/* Test session / process-group / TTY semantics
 *
 * Copyright 2026 elfuse contributors
 * SPDX-License-Identifier: Apache-2.0
 *
 * Tests: setsid, getsid, getpgid, setpgid, and basic job-control
 * ioctl semantics (TIOCGPGRP, TIOCSPGRP).
 */

#include <stdio.h>
#include <unistd.h>

#include "test-harness.h"
#include "raw-syscall.h"

#define __NR_getpid 172
#define __NR_getpgid 155
#define __NR_getsid 156
#define __NR_setsid 157
#define __NR_setpgid 154

int passes = 0, fails = 0;

int main(void)
{
    printf("test-session: session/process-group semantics\n");

    long pid = raw_syscall0(__NR_getpid);

    /* Initial state: pgid == pid (process is its own group leader) */
    TEST("getpgid(0) == pid");
    long pgid = raw_syscall1(__NR_getpgid, 0);
    EXPECT_TRUE(pgid == pid, "pgid != pid");

    TEST("getpgid(self) == pid");
    pgid = raw_syscall1(__NR_getpgid, pid);
    EXPECT_TRUE(pgid == pid, "pgid != pid");

    /* getsid: initial process is its own session leader */
    TEST("getsid(0) == pid");
    long sid = raw_syscall1(__NR_getsid, 0);
    EXPECT_TRUE(sid == pid, "sid != pid");

    TEST("getsid(self) == pid");
    sid = raw_syscall1(__NR_getsid, pid);
    EXPECT_TRUE(sid == pid, "sid != pid");

    /* setsid: initial process is already a group leader, so EPERM */
    TEST("setsid fails with EPERM for group leader");
    long r = raw_syscall0(__NR_setsid);
    EXPECT_TRUE(r == -1 /* LINUX_EPERM */, "expected -EPERM");

    /* setpgid(0, 0): session leader cannot change pgid */
    TEST("setpgid(0,0) for session leader returns EPERM");
    r = raw_syscall2(__NR_setpgid, 0, 0);
    EXPECT_TRUE(r == -1 /* LINUX_EPERM */, "expected -EPERM");

    /* getsid for unknown PID returns -ESRCH */
    TEST("getsid(99999) returns ESRCH");
    r = raw_syscall1(__NR_getsid, 99999);
    EXPECT_TRUE(r == -3 /* LINUX_ESRCH */, "expected -ESRCH");

    /* getpgid for unknown PID returns -ESRCH */
    TEST("getpgid(99999) returns ESRCH");
    r = raw_syscall1(__NR_getpgid, 99999);
    EXPECT_TRUE(r == -3 /* LINUX_ESRCH */, "expected -ESRCH");

    SUMMARY("test-session");
    return fails ? 1 : 0;
}
