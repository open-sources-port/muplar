/* Sysroot no-follow regression tests
 *
 * Copyright 2026 elfuse contributors
 * Copyright 2025 Moritz Angermann, zw3rk pte. ltd.
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>

#include "test-harness.h"

#ifndef SYS_statx
#define SYS_statx 291
#endif

int passes = 0, fails = 0;

int main(void)
{
    const char *link_path = "/tmp/elfuse-sysroot-nofollow-link";
    char link_buf[256];

    printf("test-sysroot-nofollow: sysroot no-follow tests\n");

    TEST("readlinkat absolute symlink");
    {
        ssize_t len = readlink(link_path, link_buf, sizeof(link_buf) - 1);
        if (len > 0) {
            link_buf[len] = '\0';
            if (!strcmp(link_buf, "/outside-target"))
                PASS();
            else
                FAIL("readlink returned wrong target");
        } else {
            FAIL("readlink failed");
        }
    }

    TEST("lstat absolute symlink");
    {
        struct stat st;
        if (lstat(link_path, &st) == 0 && S_ISLNK(st.st_mode))
            PASS();
        else
            FAIL("lstat did not see a symlink");
    }

    TEST("statx AT_SYMLINK_NOFOLLOW");
    {
        struct statx sx;
        memset(&sx, 0, sizeof(sx));
        if (syscall(SYS_statx, AT_FDCWD, link_path, AT_SYMLINK_NOFOLLOW, 0x7ff,
                    &sx) == 0 &&
            S_ISLNK(sx.stx_mode))
            PASS();
        else
            FAIL("statx did not preserve symlink semantics");
    }

    TEST("open O_NOFOLLOW");
    {
        errno = 0;
        int fd = open(link_path, O_RDONLY | O_NOFOLLOW);
        if (fd < 0 && errno == ELOOP)
            PASS();
        else {
            if (fd >= 0)
                close(fd);
            FAIL("open did not fail with ELOOP");
        }
    }

    SUMMARY("test-sysroot-nofollow");
    return fails > 0 ? 1 : 0;
}
