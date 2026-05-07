/* Test emulated UID/GID, capabilities, priority, and affinity
 *
 * Copyright 2026 elfuse contributors
 * SPDX-License-Identifier: Apache-2.0
 *
 * Tests: setuid, setgid, setresuid, setresgid, setreuid, setregid,
 *        getresuid, getresgid, capset, setpriority, getpriority,
 *        sched_setaffinity
 */

#include "test-harness.h"
#include "raw-syscall.h"

/* aarch64 Linux syscall numbers */
#define __NR_setuid 146
#define __NR_setgid 144
#define __NR_setresuid 147
#define __NR_getresuid 148
#define __NR_getresgid 150
#define __NR_setreuid 145
#define __NR_capset 91
#define __NR_setpriority 140
#define __NR_getpriority 141
#define __NR_sched_setaffinity 122
#define __NR_sched_getaffinity 123
#define __NR_getuid 174
#define __NR_geteuid 175
#define __NR_getgid 176

int main(void)
{
    int passes = 0, fails = 0;

    printf("test-credentials: UID/GID, capabilities, priority, affinity\n");

    /* Baseline: UID/GID should be 1000 */
    TEST("getuid == 1000");
    EXPECT_TRUE(raw_syscall0(__NR_getuid) == 1000, "unexpected uid");

    TEST("geteuid == 1000");
    EXPECT_TRUE(raw_syscall0(__NR_geteuid) == 1000, "unexpected euid");

    TEST("getgid == 1000");
    EXPECT_TRUE(raw_syscall0(__NR_getgid) == 1000, "unexpected gid");

    /* getresuid returns coherent triple */
    TEST("getresuid coherent");
    {
        unsigned int ruid = 0, euid = 0, suid = 0;
        long rc = raw_syscall3(__NR_getresuid, (long) &ruid, (long) &euid,
                               (long) &suid);
        EXPECT_TRUE(rc == 0 && ruid == 1000 && euid == 1000 && suid == 1000,
                    "getresuid mismatch");
    }

    /* setuid: can set euid to current ruid (1000), no-op but must succeed */
    TEST("setuid(1000) succeeds");
    EXPECT_TRUE(raw_syscall1(__NR_setuid, 1000) == 0, "setuid(1000) failed");

    /* setuid: setting to arbitrary value must fail with -EPERM */
    TEST("setuid(0) returns -EPERM");
    {
        long rc = raw_syscall1(__NR_setuid, 0);
        if (rc == -1) /* -EPERM */
            PASS();
        else
            FAIL("expected -EPERM");
    }

    /* setresuid: swap euid to 1000 (already 1000, but tests the path) */
    TEST("setresuid(-1,-1,-1) no-op");
    {
        long rc = raw_syscall3(__NR_setresuid, (long) (unsigned) -1,
                               (long) (unsigned) -1, (long) (unsigned) -1);
        EXPECT_TRUE(rc == 0, "setresuid no-op failed");
    }

    /* setresuid: set euid to a value not in {ruid, euid, suid} */
    TEST("setresuid(-1,42,-1) returns -EPERM");
    {
        long rc = raw_syscall3(__NR_setresuid, (long) (unsigned) -1, 42,
                               (long) (unsigned) -1);
        if (rc == -1) /* -EPERM */
            PASS();
        else
            FAIL("expected -EPERM");
    }

    /* setreuid: ruid can only be set to current ruid or euid */
    TEST("setreuid(1000,-1) succeeds");
    {
        long rc = raw_syscall2(__NR_setreuid, 1000, (long) (unsigned) -1);
        EXPECT_TRUE(rc == 0, "setreuid(ruid,-1) failed");
    }

    TEST("setreuid(42,-1) returns -EPERM");
    {
        long rc = raw_syscall2(__NR_setreuid, 42, (long) (unsigned) -1);
        if (rc == -1) /* -EPERM */
            PASS();
        else
            FAIL("expected -EPERM");
    }

    /* getresgid */
    TEST("getresgid coherent");
    {
        unsigned int rgid = 0, egid = 0, sgid = 0;
        long rc = raw_syscall3(__NR_getresgid, (long) &rgid, (long) &egid,
                               (long) &sgid);
        EXPECT_TRUE(rc == 0 && rgid == 1000 && egid == 1000 && sgid == 1000,
                    "getresgid mismatch");
    }

    /* setgid: same constraints as setuid */
    TEST("setgid(1000) succeeds");
    EXPECT_TRUE(raw_syscall1(__NR_setgid, 1000) == 0, "setgid(1000) failed");

    TEST("setgid(0) returns -EPERM");
    EXPECT_TRUE(raw_syscall1(__NR_setgid, 0) == -1, "expected -EPERM");

    /* capset: unprivileged process cannot set capabilities */
    TEST("capset returns -EPERM");
    {
        /* Linux capset header: version=0x20080522, pid=0 */
        unsigned int hdr[2] = {0x20080522, 0};
        unsigned int data[6] = {0};
        long rc = raw_syscall2(__NR_capset, (long) hdr, (long) data);
        if (rc == -1) /* -EPERM */
            PASS();
        else
            FAIL("expected -EPERM");
    }

    /* setpriority/getpriority coherence */
    TEST("setpriority(0) then getpriority");
    {
        /* PRIO_PROCESS=0, who=0 (self), prio=5 */
        long rc = raw_syscall3(__NR_setpriority, 0, 0, 5);
        if (rc != 0) {
            FAIL("setpriority failed");
        } else {
            /* Linux getpriority returns 20-nice, so nice=5 -> return 15 */
            long prio = raw_syscall2(__NR_getpriority, 0, 0);
            EXPECT_TRUE(prio == 15, "getpriority mismatch");
        }
        /* Reset nice to 0 */
        raw_syscall3(__NR_setpriority, 0, 0, 0);
    }

    /* sched_setaffinity: mask with CPU 0 should succeed */
    TEST("sched_setaffinity(CPU0)");
    {
        unsigned long mask = 1; /* CPU 0 */
        long rc =
            raw_syscall3(__NR_sched_setaffinity, 0, sizeof(mask), (long) &mask);
        EXPECT_TRUE(rc == 0, "setaffinity with CPU0 failed");
    }

    /* sched_setaffinity: empty mask should fail */
    TEST("sched_setaffinity(empty) fails");
    {
        unsigned long mask = 0;
        long rc =
            raw_syscall3(__NR_sched_setaffinity, 0, sizeof(mask), (long) &mask);
        EXPECT_TRUE(rc != 0, "expected failure for empty mask");
    }

    /* sched_getaffinity: should report CPU 0 */
    TEST("sched_getaffinity has CPU0");
    {
        unsigned long mask = 0;
        long rc =
            raw_syscall3(__NR_sched_getaffinity, 0, sizeof(mask), (long) &mask);
        EXPECT_TRUE(rc > 0 && (mask & 1), "CPU0 not in mask");
    }

    SUMMARY("test-credentials");
    return fails ? 1 : 0;
}
