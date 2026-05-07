/* /proc/self/* completeness tests
 *
 * Copyright 2026 elfuse contributors
 * Copyright 2025 Moritz Angermann, zw3rk pte. ltd.
 * SPDX-License-Identifier: Apache-2.0
 *
 * Tests:
 *   1. /proc/self/auxv returns valid auxv with AT_PAGESZ=4096
 *   2. /proc/self/environ contains at least one entry
 *   3. /proc/self/cmdline is non-empty
 *   4. /proc/self/maps contains [heap] and [stack]
 *   5. /proc/self/status contains correct PID
 *
 * Syscalls: openat(56), read(63), close(57), getpid(172)
 */

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "test-harness.h"
#include "test-util.h"

int passes = 0, fails = 0;

#define AT_NULL 0
#define AT_PAGESZ 6

int main(void)
{
    char buf[4096] __attribute__((aligned(8)));
    ssize_t n;

    TEST("procfs: /proc/self/auxv readable");
    {
        n = raw_read_file_nul("/proc/self/auxv", buf, sizeof(buf));
        if (n < 0) {
            FAIL("open failed");
        } else if (n > 0) {
            PASS();
        } else {
            FAIL("empty auxv");
        }
    }

    TEST("procfs: auxv contains AT_PAGESZ=4096");
    {
        n = raw_read_file_nul("/proc/self/auxv", buf, sizeof(buf));
        if (n < 0) {
            FAIL("open failed");
        } else {
            bool found = false;
            uint64_t *p = (uint64_t *) buf;
            for (ssize_t i = 0; i + 1 < n / 8; i += 2) {
                if (p[i] == AT_PAGESZ && p[i + 1] == 4096) {
                    found = true;
                    break;
                }
                if (p[i] == AT_NULL)
                    break;
            }
            EXPECT_TRUE(found, "AT_PAGESZ not found");
        }
    }

    TEST("procfs: /proc/self/environ readable");
    {
        n = raw_read_file_nul("/proc/self/environ", buf, sizeof(buf));
        if (n < 0) {
            FAIL("open failed");
        } else if (n > 0) {
            PASS();
        } else {
            FAIL("empty environ");
        }
    }

    TEST("procfs: /proc/self/cmdline non-empty");
    {
        n = raw_read_file_nul("/proc/self/cmdline", buf, sizeof(buf));
        if (n < 0) {
            FAIL("open failed");
        } else if (n > 0) {
            PASS();
        } else {
            FAIL("empty cmdline");
        }
    }

    TEST("procfs: /proc/self/maps contains [stack] and [heap]");
    {
        n = raw_read_file_nul("/proc/self/maps", buf, sizeof(buf));
        if (n < 0) {
            FAIL("open failed");
        } else {
            if (n > 0) {
                if (strstr(buf, "[stack]") && strstr(buf, "[heap]"))
                    PASS();
                else
                    FAIL("stack or heap not found in maps");
            } else {
                FAIL("empty maps");
            }
        }
    }

    TEST("procfs: /proc/self/status has correct PID");
    {
        long pid = raw_getpid();
        n = raw_read_file_nul("/proc/self/status", buf, sizeof(buf));
        if (n < 0) {
            FAIL("open failed");
        } else {
            if (n > 0) {
                /* Check Pid: field */
                bool found = false;
                for (ssize_t i = 0; i < n - 5; i++) {
                    if (buf[i] == 'P' && buf[i + 1] == 'i' &&
                        buf[i + 2] == 'd' && buf[i + 3] == ':') {
                        /* Parse the PID value */
                        ssize_t j = i + 4;
                        while (j < n && (buf[j] == ' ' || buf[j] == '\t'))
                            j++;
                        long parsed_pid = 0;
                        while (j < n && buf[j] >= '0' && buf[j] <= '9')
                            parsed_pid = parsed_pid * 10 + (buf[j++] - '0');
                        if (parsed_pid == pid)
                            found = true;
                        break;
                    }
                }
                EXPECT_TRUE(found, "PID mismatch in status");
            } else {
                FAIL("empty status");
            }
        }
    }

    SUMMARY("test-procfs");
    return fails > 0 ? 1 : 0;
}
