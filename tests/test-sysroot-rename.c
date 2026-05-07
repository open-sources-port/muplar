/* Sysroot rename regression tests
 *
 * Copyright 2026 elfuse contributors
 * Copyright 2025 Moritz Angermann, zw3rk pte. ltd.
 * SPDX-License-Identifier: Apache-2.0
 */

#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>

#include "test-harness.h"
#include "test-util.h"

#ifndef SYS_renameat2
#define SYS_renameat2 276
#endif

int passes = 0, fails = 0;

int main(void)
{
    const char *src = "/tmp/elfuse-sysroot-rename-src.txt";
    const char *dst = "/tmp/elfuse-sysroot-rename-dst.txt";

    printf("test-sysroot-rename: sysroot rename tests\n");

    TEST("renameat2 absolute destination");
    {
        struct stat st;
        char buf[32];
        long r = syscall(SYS_renameat2, AT_FDCWD, src, AT_FDCWD, dst, 0);

        if (r == 0 && stat(src, &st) == -1 &&
            read_file_nul(dst, buf, sizeof(buf)) > 0 &&
            !strcmp(buf, "inside-sysroot\n"))
            PASS();
        else
            FAIL("renameat2 did not create destination in sysroot");
    }

    SUMMARY("test-sysroot-rename");
    return fails > 0 ? 1 : 0;
}
