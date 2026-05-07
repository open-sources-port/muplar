/* Shared test macros for unit tests
 *
 * Copyright 2026 elfuse contributors
 * Copyright 2025 Moritz Angermann, zw3rk pte. ltd.
 * SPDX-License-Identifier: Apache-2.0
 *
 * Provides TEST/PASS/FAIL macros used by all test-*.c files.
 * Each test file must declare int passes = 0, fails = 0; before using.
 */

#pragma once

#include <stdio.h>
#include <errno.h>

#define TEST(name) printf("  %-30s ", name)
#define PASS()          \
    do {                \
        printf("OK\n"); \
        passes++;       \
    } while (0)
#define FAIL(msg)                                    \
    do {                                             \
        printf("FAIL: %s (errno=%d)\n", msg, errno); \
        fails++;                                     \
    } while (0)
#define SUMMARY(name)                                                 \
    do {                                                              \
        printf("\n%s: %d passed, %d failed%s\n", name, passes, fails, \
               fails == 0 ? " - PASS" : " - FAIL");                   \
    } while (0)

/* Pass if expr returned -1 with the given errno; otherwise fail with msg.
 * Suitable for any integer-typed syscall-style return (int, long, ssize_t).
 */
#define EXPECT_ERRNO(expr, expected_errno, msg)      \
    do {                                             \
        long _eer = (long) (expr);                   \
        if (_eer == -1 && errno == (expected_errno)) \
            PASS();                                  \
        else                                         \
            FAIL(msg);                               \
    } while (0)

/* Pass if a raw-syscall-style expression returned the given negative Linux
 * errno value (e.g., -EFAULT == -14). Used by tests that bypass libc and
 * read the kernel return value directly.
 */
#define EXPECT_RAW_ERRNO(expr, expected_neg, msg) \
    do {                                          \
        long _rer = (long) (expr);                \
        if (_rer == (long) (expected_neg))        \
            PASS();                               \
        else                                      \
            FAIL(msg);                            \
    } while (0)

/* Pass if cond evaluates true; otherwise FAIL with msg. Replaces the
 * recurring "if (cond) PASS(); else FAIL(msg);" idiom.
 */
#define EXPECT_TRUE(cond, msg) \
    do {                       \
        if (cond)              \
            PASS();            \
        else                   \
            FAIL(msg);         \
    } while (0)

/* Pass if two integer expressions are equal; otherwise FAIL with msg. */
#define EXPECT_EQ(a, b, msg)          \
    do {                              \
        if ((long) (a) == (long) (b)) \
            PASS();                   \
        else                          \
            FAIL(msg);                \
    } while (0)
