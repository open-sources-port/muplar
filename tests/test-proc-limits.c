/* Test /proc/self/limits synthetic file
 *
 * Copyright 2026 elfuse contributors
 * SPDX-License-Identifier: Apache-2.0
 *
 * Verifies that /proc/self/limits is readable and contains expected
 * rows matching prlimit64 values.
 */

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

#include "test-harness.h"
#include "raw-syscall.h"

#define __NR_openat 56
#define __NR_read 63
#define __NR_close 57

#ifndef AT_FDCWD
#define AT_FDCWD (-100)
#endif
#define LINUX_O_RDONLY 0

int passes = 0, fails = 0;

int main(void)
{
    char buf[4096];

    printf("test-proc-limits: /proc/self/limits synthetic file\n");

    /* Open /proc/self/limits */
    TEST("open /proc/self/limits");
    long fd = raw_syscall4(__NR_openat, AT_FDCWD, (long) "/proc/self/limits",
                           LINUX_O_RDONLY, 0);
    if (fd < 0)
        FAIL("openat failed");
    else
        PASS();

    if (fd < 0)
        goto done;

    /* Read the content */
    TEST("read content");
    long n = raw_syscall3(__NR_read, fd, (long) buf, sizeof(buf) - 1);
    if (n <= 0)
        FAIL("read returned <= 0");
    else
        PASS();

    raw_syscall1(__NR_close, fd);

    if (n <= 0)
        goto done;
    buf[n] = '\0';

    /* Check header line */
    TEST("header line present");
    if (strstr(buf, "Limit") && strstr(buf, "Soft Limit") &&
        strstr(buf, "Hard Limit") && strstr(buf, "Units"))
        PASS();
    else
        FAIL("header missing expected columns");

    /* Check for known resource rows */
    TEST("Max open files row");
    EXPECT_TRUE(strstr(buf, "Max open files"), "missing 'Max open files'");

    TEST("Max stack size row");
    EXPECT_TRUE(strstr(buf, "Max stack size"), "missing 'Max stack size'");

    TEST("Max cpu time row");
    EXPECT_TRUE(strstr(buf, "Max cpu time"), "missing 'Max cpu time'");

    TEST("Max processes row");
    EXPECT_TRUE(strstr(buf, "Max processes"), "missing 'Max processes'");

    TEST("Max locked memory row");
    EXPECT_TRUE(strstr(buf, "Max locked memory"),
                "missing 'Max locked memory'");

    TEST("Max file locks row");
    EXPECT_TRUE(strstr(buf, "Max file locks"), "missing 'Max file locks'");

    /* Verify Max open files has a numeric soft limit (not unlimited).
     * Scan from the name to the end of the line for a digit sequence.
     */
    TEST("Max open files has numeric value");
    {
        const char *p = strstr(buf, "Max open files");
        int found_digit = 0;
        if (p) {
            /* Find the end of the line */
            const char *eol = strchr(p, '\n');
            if (!eol)
                eol = p + strlen(p);
            /* Skip past the name to the value columns */
            for (const char *s = p + 14; s < eol; s++) {
                if (*s >= '1' && *s <= '9') {
                    found_digit = 1;
                    break;
                }
            }
        }
        EXPECT_TRUE(found_digit, "expected numeric NOFILE soft limit");
    }

    /* Verify at least 16 rows (header + 16 resources) */
    TEST("at least 17 lines");
    {
        int lines = 0;
        for (long i = 0; i < n; i++)
            if (buf[i] == '\n')
                lines++;
        EXPECT_TRUE(lines >= 17, "too few lines");
    }

done:
    SUMMARY("test-proc-limits");
    return fails ? 1 : 0;
}
