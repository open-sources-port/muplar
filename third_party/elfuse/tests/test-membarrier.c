/* Test membarrier syscall
 *
 * Copyright 2026 elfuse contributors
 * SPDX-License-Identifier: Apache-2.0
 *
 * Tests: CMD_QUERY, CMD_GLOBAL, CMD_PRIVATE_EXPEDITED, registration,
 *        unknown commands, invalid flags
 */

#include "test-harness.h"
#include "raw-syscall.h"

#define __NR_membarrier 283

#define MEMBARRIER_CMD_QUERY 0
#define MEMBARRIER_CMD_GLOBAL (1 << 0)
#define MEMBARRIER_CMD_GLOBAL_EXPEDITED (1 << 1)
#define MEMBARRIER_CMD_REGISTER_GLOBAL_EXPEDITED (1 << 2)
#define MEMBARRIER_CMD_PRIVATE_EXPEDITED (1 << 3)
#define MEMBARRIER_CMD_REGISTER_PRIVATE_EXPEDITED (1 << 4)

int main(void)
{
    int passes = 0, fails = 0;

    printf("test-membarrier: membarrier syscall tests\n");

    /* CMD_QUERY should return supported bitmask */
    TEST("CMD_QUERY returns bitmask");
    {
        long rc = raw_syscall2(__NR_membarrier, MEMBARRIER_CMD_QUERY, 0);
        EXPECT_TRUE(rc > 0 && (rc & MEMBARRIER_CMD_GLOBAL),
                    "CMD_QUERY did not report CMD_GLOBAL");
    }

    /* CMD_QUERY should include PRIVATE_EXPEDITED */
    TEST("CMD_QUERY has PRIVATE_EXPEDITED");
    {
        long rc = raw_syscall2(__NR_membarrier, MEMBARRIER_CMD_QUERY, 0);
        EXPECT_TRUE(rc > 0 && (rc & MEMBARRIER_CMD_PRIVATE_EXPEDITED),
                    "missing PRIVATE_EXPEDITED");
    }

    /* CMD_GLOBAL should succeed */
    TEST("CMD_GLOBAL succeeds");
    {
        long rc = raw_syscall2(__NR_membarrier, MEMBARRIER_CMD_GLOBAL, 0);
        EXPECT_TRUE(rc == 0, "CMD_GLOBAL failed");
    }

    /* CMD_GLOBAL_EXPEDITED should succeed */
    TEST("CMD_GLOBAL_EXPEDITED succeeds");
    {
        long rc =
            raw_syscall2(__NR_membarrier, MEMBARRIER_CMD_GLOBAL_EXPEDITED, 0);
        EXPECT_TRUE(rc == 0, "CMD_GLOBAL_EXPEDITED failed");
    }

    /* Registration commands should succeed */
    TEST("REGISTER_GLOBAL_EXPEDITED");
    {
        long rc = raw_syscall2(__NR_membarrier,
                               MEMBARRIER_CMD_REGISTER_GLOBAL_EXPEDITED, 0);
        EXPECT_TRUE(rc == 0, "register failed");
    }

    TEST("REGISTER_PRIVATE_EXPEDITED");
    {
        long rc = raw_syscall2(__NR_membarrier,
                               MEMBARRIER_CMD_REGISTER_PRIVATE_EXPEDITED, 0);
        EXPECT_TRUE(rc == 0, "register failed");
    }

    /* CMD_PRIVATE_EXPEDITED should succeed */
    TEST("CMD_PRIVATE_EXPEDITED succeeds");
    {
        long rc =
            raw_syscall2(__NR_membarrier, MEMBARRIER_CMD_PRIVATE_EXPEDITED, 0);
        EXPECT_TRUE(rc == 0, "CMD_PRIVATE_EXPEDITED failed");
    }

    /* Unknown command should return -EINVAL */
    TEST("unknown cmd returns -EINVAL");
    {
        long rc = raw_syscall2(__NR_membarrier, 0x8000, 0);
        if (rc == -22) /* -EINVAL */
            PASS();
        else
            FAIL("expected -EINVAL");
    }

    /* Non-zero flags should return -EINVAL */
    TEST("flags!=0 returns -EINVAL");
    {
        long rc = raw_syscall2(__NR_membarrier, MEMBARRIER_CMD_QUERY, 1);
        if (rc == -22) /* -EINVAL */
            PASS();
        else
            FAIL("expected -EINVAL");
    }

    SUMMARY("test-membarrier");
    return fails ? 1 : 0;
}
