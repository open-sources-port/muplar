/* Sysroot chdir regression tests
 *
 * Copyright 2026 elfuse contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "test-harness.h"

int passes = 0, fails = 0;

int main(void)
{
    char cwd[256];
    char proc_cwd[256];

    printf("test-sysroot-chdir: sysroot chdir tests\n");

    TEST("absolute chdir keeps guest cwd");
    {
        ssize_t len;

        if (chdir("/bin") < 0) {
            FAIL("chdir /bin failed");
        } else if (!getcwd(cwd, sizeof(cwd))) {
            FAIL("getcwd after chdir /bin failed");
        } else if (strcmp(cwd, "/bin") != 0) {
            FAIL("getcwd leaked host sysroot path");
        } else if ((len = readlink("/proc/self/cwd", proc_cwd,
                                   sizeof(proc_cwd) - 1)) < 0) {
            FAIL("readlink /proc/self/cwd failed");
        } else {
            proc_cwd[len] = '\0';
            if (strcmp(proc_cwd, "/bin") != 0)
                FAIL("/proc/self/cwd leaked host sysroot path");
            else
                PASS();
        }
    }

    TEST("relative chdir stays guest-visible under sysroot");
    {
        if (chdir("../lib") < 0) {
            FAIL("chdir ../lib failed");
        } else if (!getcwd(cwd, sizeof(cwd))) {
            FAIL("getcwd after chdir ../lib failed");
        } else if (strcmp(cwd, "/lib") != 0) {
            FAIL("relative chdir produced wrong guest cwd");
        } else {
            PASS();
        }
    }

    TEST("missing absolute path does not fall back to sysroot lib basename");
    {
        errno = 0;
        if (chdir("/elfuse-sysroot-shadow") == 0) {
            FAIL("chdir unexpectedly succeeded via sysroot lib fallback");
        } else if (errno != ENOENT) {
            FAIL("chdir failed with wrong errno");
        } else {
            PASS();
        }
    }

    SUMMARY("test-sysroot-chdir");
    return fails > 0 ? 1 : 0;
}
