/* O_PATH semantics tests
 *
 * Copyright 2026 elfuse contributors
 * SPDX-License-Identifier: Apache-2.0
 *
 * Linux O_PATH descriptors carry only a path reference: read/write/lseek/
 * ioctl/fsync/flock/ftruncate/fchmod/fchown/getdents/fsetxattr/fremovexattr
 * must all return EBADF.  fstat/fstatfs/close/dup/fcntl-cloexec/fchdir and
 * use as a *at() dirfd remain valid.
 */

#define _GNU_SOURCE
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <sys/xattr.h>
#include <termios.h>
#include <unistd.h>

#include "test-harness.h"

#ifndef O_PATH
#define O_PATH 010000000
#endif

int passes = 0, fails = 0;

static char tmp_file[256];
static char tmp_dir[256];

static void setup_fixtures(void)
{
    snprintf(tmp_file, sizeof(tmp_file), "/tmp/elfuse-opath-%d.txt",
             (int) getpid());
    snprintf(tmp_dir, sizeof(tmp_dir), "/tmp/elfuse-opath-dir-%d",
             (int) getpid());

    int fd = open(tmp_file, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd >= 0) {
        write(fd, "hello\n", 6);
        close(fd);
    }
    mkdir(tmp_dir, 0755);
    char child[300];
    snprintf(child, sizeof(child), "%s/child", tmp_dir);
    fd = open(child, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd >= 0)
        close(fd);
}

static void teardown_fixtures(void)
{
    char child[300];
    snprintf(child, sizeof(child), "%s/child", tmp_dir);
    unlink(child);
    rmdir(tmp_dir);
    unlink(tmp_file);
}

/* Helper: open an O_PATH fd or skip the test on failure. */
static int open_path_or_fail(const char *path, const char *what)
{
    int fd = open(path, O_PATH);
    if (fd < 0)
        FAIL(what);
    return fd;
}

static void test_open_path_smoke(void)
{
    TEST("open(O_PATH) on regular file");
    int fd = open(tmp_file, O_PATH);
    if (fd < 0) {
        FAIL("open O_PATH");
        return;
    }
    PASS();

    TEST("F_GETFL reflects O_PATH");
    int fl = fcntl(fd, F_GETFL);
    EXPECT_TRUE(fl != -1 && (fl & O_PATH), "F_GETFL missing O_PATH");

    close(fd);
}

static void test_read_write_rejected(void)
{
    int fd = open_path_or_fail(tmp_file, "open");
    if (fd < 0)
        return;

    char buf[8];
    TEST("read(O_PATH) -> EBADF");
    EXPECT_ERRNO(read(fd, buf, sizeof(buf)), EBADF, "read");
    TEST("write(O_PATH) -> EBADF");
    EXPECT_ERRNO(write(fd, "x", 1), EBADF, "write");
    TEST("pread(O_PATH) -> EBADF");
    EXPECT_ERRNO(pread(fd, buf, sizeof(buf), 0), EBADF, "pread");
    TEST("pwrite(O_PATH) -> EBADF");
    EXPECT_ERRNO(pwrite(fd, "x", 1, 0), EBADF, "pwrite");

    struct iovec iov = {.iov_base = buf, .iov_len = sizeof(buf)};
    TEST("readv(O_PATH) -> EBADF");
    EXPECT_ERRNO(readv(fd, &iov, 1), EBADF, "readv");
    iov.iov_base = "x";
    iov.iov_len = 1;
    TEST("writev(O_PATH) -> EBADF");
    EXPECT_ERRNO(writev(fd, &iov, 1), EBADF, "writev");

    close(fd);
}

static void test_seek_truncate_sync_lock_rejected(void)
{
    int fd = open_path_or_fail(tmp_file, "open");
    if (fd < 0)
        return;

    TEST("lseek(O_PATH) -> EBADF");
    EXPECT_ERRNO(lseek(fd, 0, SEEK_SET), EBADF, "lseek");
    TEST("ftruncate(O_PATH) -> EBADF");
    EXPECT_ERRNO(ftruncate(fd, 0), EBADF, "ftruncate");
    TEST("fsync(O_PATH) -> EBADF");
    EXPECT_ERRNO(fsync(fd), EBADF, "fsync");
    TEST("fdatasync(O_PATH) -> EBADF");
    EXPECT_ERRNO(fdatasync(fd), EBADF, "fdatasync");
    TEST("flock(O_PATH) -> EBADF");
    EXPECT_ERRNO(flock(fd, LOCK_EX | LOCK_NB), EBADF, "flock");

    close(fd);
}

static void test_ioctl_fchmod_fchown_rejected(void)
{
    int fd = open_path_or_fail(tmp_file, "open");
    if (fd < 0)
        return;

    int avail = 0;
    TEST("ioctl(O_PATH, FIONREAD) -> EBADF");
    EXPECT_ERRNO(ioctl(fd, FIONREAD, &avail), EBADF, "ioctl");
    TEST("fchmod(O_PATH) -> EBADF");
    EXPECT_ERRNO(fchmod(fd, 0644), EBADF, "fchmod");
    TEST("fchown(O_PATH) -> EBADF");
    EXPECT_ERRNO(fchown(fd, getuid(), getgid()), EBADF, "fchown");

    close(fd);
}

static void test_xattr_rejected(void)
{
    int fd = open_path_or_fail(tmp_file, "open");
    if (fd < 0)
        return;

    /* The setxattr/removexattr path may also return ENOTSUP/EOPNOTSUPP on
     * filesystems that lack xattr support.  Only require EBADF from elfuse;
     * skip these checks on a filesystem that rejects xattr outright on a
     * fresh /tmp file (rare; APFS and ext4 both honor user.* xattrs).
     */
    int probe = setxattr(tmp_file, "user.elfuse_probe", "x", 1, 0);
    if (probe < 0 && errno != EEXIST) {
        if (errno == ENOTSUP || errno == EOPNOTSUPP) {
            close(fd);
            return;
        }
    } else {
        removexattr(tmp_file, "user.elfuse_probe");
    }

    TEST("fsetxattr(O_PATH) -> EBADF");
    EXPECT_ERRNO(fsetxattr(fd, "user.elfuse_test", "v", 1, 0), EBADF,
                 "fsetxattr");
    TEST("fremovexattr(O_PATH) -> EBADF");
    EXPECT_ERRNO(fremovexattr(fd, "user.elfuse_test"), EBADF, "fremovexattr");

    close(fd);
}

static void test_getdents_rejected(void)
{
    int fd = open(tmp_dir, O_PATH | O_DIRECTORY);
    if (fd < 0) {
        FAIL("open O_PATH dir");
        return;
    }

    /* glibc's readdir() works through fdopendir, but fdopendir itself uses
     * fstat + getdents64.  The kernel returns EBADF on getdents64; glibc
     * surfaces that via readdir errno or fdopendir failure.  Use the raw
     * syscall to keep the contract precise.
     */
    char buf[1024];
    TEST("getdents64(O_PATH dir) -> EBADF");
    long n = syscall(SYS_getdents64, fd, buf, sizeof(buf));
    if (n == -1 && errno == EBADF)
        PASS();
    else
        FAIL("getdents64");

    close(fd);
}

static void test_allowed_on_path_fd(void)
{
    int fd = open_path_or_fail(tmp_file, "open");
    if (fd < 0)
        return;

    struct stat st;
    TEST("fstat(O_PATH) succeeds");
    EXPECT_TRUE(fstat(fd, &st) == 0 && S_ISREG(st.st_mode), "fstat");

    TEST("dup(O_PATH) succeeds");
    int dupfd = dup(fd);
    if (dupfd < 0) {
        FAIL("dup");
    } else {
        int fl = fcntl(dupfd, F_GETFL);
        if (fl != -1 && (fl & O_PATH))
            PASS();
        else
            FAIL("dup lost O_PATH");
        close(dupfd);
    }

    TEST("F_SETFD/F_GETFD on O_PATH");
    if (fcntl(fd, F_SETFD, FD_CLOEXEC) == 0 &&
        (fcntl(fd, F_GETFD) & FD_CLOEXEC))
        PASS();
    else
        FAIL("fcntl FD_CLOEXEC");

    close(fd);
}

static void test_path_dir_as_dirfd(void)
{
    int dfd = open(tmp_dir, O_PATH | O_DIRECTORY);
    if (dfd < 0) {
        FAIL("open O_PATH dir");
        return;
    }

    struct stat st;
    TEST("fstatat(O_PATH dirfd, \"child\")");
    EXPECT_TRUE(fstatat(dfd, "child", &st, 0) == 0, "fstatat via O_PATH");

    TEST("openat(O_PATH dirfd, \"child\")");
    int childfd = openat(dfd, "child", O_RDONLY);
    if (childfd >= 0) {
        PASS();
        close(childfd);
    } else {
        FAIL("openat via O_PATH");
    }

    TEST("fchdir(O_PATH dir)");
    EXPECT_TRUE(fchdir(dfd) == 0, "fchdir via O_PATH");

    close(dfd);
}

int main(void)
{
    printf("test-opath: O_PATH semantics tests\n");

    setup_fixtures();

    test_open_path_smoke();
    test_read_write_rejected();
    test_seek_truncate_sync_lock_rejected();
    test_ioctl_fchmod_fchown_rejected();
    test_xattr_rejected();
    test_getdents_rejected();
    test_allowed_on_path_fd();
    test_path_dir_as_dirfd();

    teardown_fixtures();

    SUMMARY("test-opath");
    return fails > 0 ? 1 : 0;
}
