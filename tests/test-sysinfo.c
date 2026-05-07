/* Test process/system info syscalls (Batch 2)
 *
 * Copyright 2026 elfuse contributors
 * Copyright 2025 Moritz Angermann, zw3rk pte. ltd.
 * SPDX-License-Identifier: Apache-2.0
 *
 * Tests: sysinfo, getrusage, getgroups, prlimit64, getppid, sched_getaffinity
 */

#include <string.h>
#include <unistd.h>
#include <sys/sysinfo.h>
#include <sys/resource.h>
#include <sched.h>

#include "test-harness.h"

int main(void)
{
    int passes = 0, fails = 0;

    printf("test-sysinfo: Batch 2 process/system info tests\n");

    /* Test sysinfo */
    TEST("sysinfo");
    {
        struct sysinfo si;
        if (sysinfo(&si) == 0) {
            EXPECT_TRUE(si.uptime > 0 && si.totalram > 0 && si.mem_unit > 0,
                        "invalid values");
        } else
            FAIL("sysinfo failed");
    }

    /* Test getrusage */
    TEST("getrusage");
    {
        struct rusage usage;
        if (getrusage(RUSAGE_SELF, &usage) == 0) {
            EXPECT_TRUE(usage.ru_maxrss >= 0, "invalid maxrss");
        } else
            FAIL("getrusage failed");
    }

    /* Test getgroups */
    TEST("getgroups");
    {
        int ngroups = getgroups(0, NULL);
        if (ngroups >= 0) {
            /* Verify the test can also read the actual groups */
            if (ngroups > 0) {
                gid_t groups[64];
                int n = getgroups(ngroups > 64 ? 64 : ngroups, groups);
                EXPECT_TRUE(n >= 0, "getgroups read failed");
            } else
                PASS();
        } else
            FAIL("getgroups count failed");
    }

    /* Test prlimit64 (via getrlimit/setrlimit) */
    TEST("prlimit64 (RLIMIT_NOFILE)");
    {
        struct rlimit rl;
        if (getrlimit(RLIMIT_NOFILE, &rl) == 0) {
            EXPECT_TRUE(rl.rlim_cur > 0, "invalid rlim_cur");
        } else
            FAIL("getrlimit failed");
    }

    /* Test getppid */
    TEST("getppid");
    {
        pid_t ppid = getppid();
        /* In the current single-process model, ppid should be 0 */
        EXPECT_TRUE(ppid >= 0, "getppid returned negative");
    }

    /* Test sched_getaffinity */
    TEST("sched_getaffinity");
    {
        cpu_set_t set;
        CPU_ZERO(&set);
        if (sched_getaffinity(0, sizeof(set), &set) == 0) {
            /* Should have at least 1 CPU */
            int count = CPU_COUNT(&set);
            EXPECT_TRUE(count >= 1, "no CPUs in affinity mask");
        } else
            FAIL("sched_getaffinity failed");
    }

    /* Test getpid / gettid */
    TEST("getpid");
    {
        pid_t pid = getpid();
        EXPECT_TRUE(pid > 0, "invalid pid");
    }

    SUMMARY("test-sysinfo");
    return fails > 0 ? 1 : 0;
}
