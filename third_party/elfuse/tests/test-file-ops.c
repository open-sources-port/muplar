/* Test file manipulation syscalls (Batch 1)
 *
 * Copyright 2026 elfuse contributors
 * Copyright 2025 Moritz Angermann, zw3rk pte. ltd.
 * SPDX-License-Identifier: Apache-2.0
 *
 * Tests: chmod, symlink, hardlink, utimensat, statx, faccessat2, renameat2
 */

#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>

#include "test-harness.h"
#include "test-util.h"

#ifndef SYS_statx
#define SYS_statx 291
#endif

#ifndef SYS_faccessat2
#define SYS_faccessat2 439
#endif

#ifndef SYS_renameat2
#define SYS_renameat2 276
#endif

static int create_test_file(const char *path, const char *contents)
{
    int rc = -1;
    size_t len = strlen(contents);
    int fd = open(path, O_CREAT | O_WRONLY | O_TRUNC, 0644);
    if (fd < 0)
        return -1;

    if (write_fd_all(fd, contents, len) == 0 && close(fd) == 0) {
        fd = -1;
        rc = 0;
    }

    if (fd >= 0)
        close(fd);
    if (rc < 0)
        unlink(path);
    return rc;
}

int main(void)
{
    int passes = 0, fails = 0;
    const char *testfile = "/tmp/elfuse-test-file-ops.txt";
    const char *symlink_path = "/tmp/elfuse-test-symlink";
    const char *hardlink_path = "/tmp/elfuse-test-hardlink";

    printf("test-file-ops: Batch 1 file manipulation tests\n");

    /* Clean up any leftover files */
    unlink(testfile);
    unlink(symlink_path);
    unlink(hardlink_path);

    /* Create a test file */
    int fd;
    if (create_test_file(testfile, "hello\n") < 0) {
        printf("FATAL: cannot create %s\n", testfile);
        return 1;
    }

    /* Test fchmod */
    TEST("fchmod");
    fd = open(testfile, O_RDONLY);
    if (fd >= 0) {
        if (fchmod(fd, 0755) == 0) {
            struct stat st;
            fstat(fd, &st);
            EXPECT_TRUE((st.st_mode & 0777) == 0755, "mode mismatch");
        } else
            FAIL("fchmod failed");
        close(fd);
    } else
        FAIL("open failed");

    /* Test chmod (via fchmodat) */
    TEST("chmod (fchmodat)");
    if (chmod(testfile, 0644) == 0) {
        struct stat st;
        stat(testfile, &st);
        EXPECT_TRUE((st.st_mode & 0777) == 0644, "mode mismatch");
    } else
        FAIL("chmod failed");

    /* Test symlink */
    TEST("symlink");
    if (symlink(testfile, symlink_path) == 0) {
        struct stat st;
        if (lstat(symlink_path, &st) == 0 && S_ISLNK(st.st_mode)) {
            char buf[256];
            ssize_t len = readlink(symlink_path, buf, sizeof(buf) - 1);
            if (len > 0) {
                buf[len] = '\0';
                EXPECT_TRUE(!strcmp(buf, testfile), "readlink mismatch");
            } else
                FAIL("readlink failed");
        } else
            FAIL("not a symlink");
    } else
        FAIL("symlink failed");

    /* Test hardlink */
    TEST("hardlink (link)");
    if (link(testfile, hardlink_path) == 0) {
        struct stat st1, st2;
        stat(testfile, &st1);
        stat(hardlink_path, &st2);
        EXPECT_TRUE(st1.st_ino == st2.st_ino && st1.st_nlink >= 2,
                    "inode/nlink mismatch");
    } else
        FAIL("link failed");

    /* Test utimensat (set modification time) */
    TEST("utimensat");
    {
        struct timespec times[2] = {{.tv_sec = 1000000000, .tv_nsec = 0},
                                    {.tv_sec = 1000000000, .tv_nsec = 0}};
        if (utimensat(AT_FDCWD, testfile, times, 0) == 0) {
            struct stat st;
            stat(testfile, &st);
            EXPECT_TRUE(st.st_mtime == 1000000000, "mtime not updated");
        } else {
            FAIL("utimensat failed");
        }
    }

    /* Test stat after operations */
    TEST("stat consistency");
    {
        struct stat st;
        if (stat(testfile, &st) == 0) {
            EXPECT_TRUE(st.st_size == 6 && (st.st_mode & 0777) == 0644,
                        "stat mismatch");
        } else
            FAIL("stat failed");
    }

    TEST("statx");
    {
        struct stat st;
        struct statx sx;

        memset(&sx, 0, sizeof(sx));
        if (stat(testfile, &st) != 0) {
            FAIL("stat failed");
        } else {
            long r = syscall(SYS_statx, AT_FDCWD, testfile, 0, 0x7ff, &sx);
            if (r == 0 && sx.stx_dev_major == (unsigned int) major(st.st_dev) &&
                sx.stx_dev_minor == (unsigned int) minor(st.st_dev)) {
                PASS();
            } else {
                FAIL("statx dev mismatch");
            }
        }
    }

    TEST("statx /proc/self/status");
    {
        struct statx sx;

        memset(&sx, 0, sizeof(sx));
        if (syscall(SYS_statx, AT_FDCWD, "/proc/self/status", 0, 0x7ff, &sx) ==
                0 &&
            S_ISREG(sx.stx_mode)) {
            PASS();
        } else {
            FAIL("statx /proc/self/status failed");
        }
    }

    TEST("statx /dev/shm");
    {
        struct statx sx;

        memset(&sx, 0, sizeof(sx));
        if (syscall(SYS_statx, AT_FDCWD, "/dev/shm", 0, 0x7ff, &sx) == 0 &&
            S_ISDIR(sx.stx_mode)) {
            PASS();
        } else {
            FAIL("statx /dev/shm failed");
        }
    }

    TEST("faccessat2");
    {
        long r = syscall(SYS_faccessat2, AT_FDCWD, testfile, F_OK, 0);
        EXPECT_TRUE(r == 0, "faccessat2 failed");
    }

    TEST("renameat2");
    {
        const char *renamed_path = "/tmp/elfuse-test-file-ops-renamed.txt";
        struct stat renamed_st, old_st;
        unlink(renamed_path);
        long r = syscall(SYS_renameat2, AT_FDCWD, testfile, AT_FDCWD,
                         renamed_path, 0);
        if (r == 0 && stat(renamed_path, &renamed_st) == 0 &&
            stat(testfile, &old_st) == -1 && errno == ENOENT) {
            EXPECT_TRUE(syscall(SYS_renameat2, AT_FDCWD, renamed_path, AT_FDCWD,
                                testfile, 0) == 0,
                        "renameat2 restore failed");
        } else {
            FAIL("renameat2 failed");
        }
        unlink(renamed_path);
    }

#ifndef RENAME_NOREPLACE
#define RENAME_NOREPLACE (1 << 0)
#endif
#ifndef RENAME_EXCHANGE
#define RENAME_EXCHANGE (1 << 1)
#endif

    TEST("renameat2 conflicting flags EINVAL");
    EXPECT_ERRNO(syscall(SYS_renameat2, AT_FDCWD, testfile, AT_FDCWD,
                         "/tmp/elfuse-test-no-such.txt",
                         RENAME_NOREPLACE | RENAME_EXCHANGE),
                 EINVAL, "expected EINVAL for conflicting flags");

    TEST("renameat2 NOREPLACE with existing dest");
    {
        const char *dest = "/tmp/elfuse-test-noreplace-dest.txt";
        int dfd = open(dest, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (dfd >= 0)
            close(dfd);
        long r = syscall(SYS_renameat2, AT_FDCWD, testfile, AT_FDCWD, dest,
                         RENAME_NOREPLACE);
        bool ok = (r == -1 && errno == EEXIST);
        /* Verify source still exists */
        struct stat st;
        ok = ok && (stat(testfile, &st) == 0);
        unlink(dest);
        EXPECT_TRUE(ok, "NOREPLACE should fail with EEXIST");
    }

    TEST("renameat2 NOREPLACE with dirfds");
    {
        char dir_template[] = "/tmp/elfuse-test-renameat2-dir.XXXXXX";
        char src_path[256] = {0};
        char dst_path[256] = {0};
        int dirfd = -1;
        bool dir_created = false;
        bool ok = false;

        if (!mkdtemp(dir_template)) {
            FAIL("mkdtemp failed");
            goto renameat2_dirfds_done;
        }
        dir_created = true;

        dirfd = open(dir_template, O_RDONLY | O_DIRECTORY);
        if (dirfd < 0) {
            FAIL("open dir failed");
            goto renameat2_dirfds_done;
        }

        snprintf(src_path, sizeof(src_path), "%s/src.txt", dir_template);
        snprintf(dst_path, sizeof(dst_path), "%s/dst.txt", dir_template);
        if (create_test_file(src_path, "") < 0) {
            FAIL("create src failed");
            goto renameat2_dirfds_done;
        }

        if (syscall(SYS_renameat2, dirfd, "src.txt", dirfd, "dst.txt",
                    RENAME_NOREPLACE) == 0) {
            struct stat src_st;
            struct stat dst_st;
            ok = (stat(src_path, &src_st) == -1 && errno == ENOENT &&
                  stat(dst_path, &dst_st) == 0);
        }

        EXPECT_TRUE(ok, "dirfd NOREPLACE rename failed");

    renameat2_dirfds_done:
        if (dirfd >= 0)
            close(dirfd);
        unlink(src_path);
        unlink(dst_path);
        if (dir_created)
            rmdir(dir_template);
    }

    /* Cleanup */
    unlink(hardlink_path);
    unlink(symlink_path);
    unlink(testfile);

    SUMMARY("test-file-ops");
    return fails > 0 ? 1 : 0;
}
