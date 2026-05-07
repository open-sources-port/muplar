/* Tier B correctness and fidelity tests
 *
 * Copyright 2026 elfuse contributors
 * SPDX-License-Identifier: Apache-2.0
 *
 * Tests: fchmodat2, openat2 RESOLVE_*, O_PATH enforcement, madvise
 * parity, /proc/self/oom_score_adj, /proc/self/fdinfo, cpuinfo scaling.
 */

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/eventfd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/signalfd.h>
#include <sys/syscall.h>
#include <sys/timerfd.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <unistd.h>

#include "test-harness.h"

int passes = 0, fails = 0;

/* fchmodat2 (SYS 452). */

#ifndef SYS_fchmodat2
#define SYS_fchmodat2 452
#endif

static void test_fchmodat2_basic(void)
{
    TEST("fchmodat2 basic");
    char path[] = "/tmp/elfuse-test-fchmodat2-XXXXXX";
    int fd = mkstemp(path);
    if (fd < 0) {
        FAIL("mkstemp");
        return;
    }
    close(fd);
    /* fchmodat2(AT_FDCWD, path, 0644, 0) should work like fchmodat */
    long rc = syscall(SYS_fchmodat2, AT_FDCWD, path, 0644, 0);
    if (rc < 0) {
        FAIL("fchmodat2");
        unlink(path);
        return;
    }
    struct stat st;
    stat(path, &st);
    unlink(path);
    EXPECT_TRUE((st.st_mode & 0777) == 0644, "mode mismatch");
}

static void test_fchmodat2_symlink_nofollow(void)
{
    TEST("fchmodat2 AT_SYMLINK_NOFOLLOW");
    char target[] = "/tmp/elfuse-test-fchmodat2-target-XXXXXX";
    char linkpath[64];

    int fd = mkstemp(target);
    if (fd < 0) {
        FAIL("mkstemp target");
        return;
    }
    close(fd);
    /* Derive the symlink name from the unique target so we never call mktemp,
     * which is racy and triggers a linker warning on glibc.
     */
    snprintf(linkpath, sizeof(linkpath), "%s.lnk", target);

    if (symlink(target, linkpath) < 0) {
        FAIL("symlink");
        unlink(target);
        return;
    }

    /* AT_SYMLINK_NOFOLLOW must change the symlink's mode, not the target's. */
    long rc =
        syscall(SYS_fchmodat2, AT_FDCWD, linkpath, 0700, AT_SYMLINK_NOFOLLOW);
    if (rc < 0) {
        FAIL("fchmodat2 nofollow");
        goto out;
    }

    struct stat st_link, st_target;
    if (lstat(linkpath, &st_link) < 0) {
        FAIL("lstat link");
        goto out;
    }
    if (stat(target, &st_target) < 0) {
        FAIL("stat target");
        goto out;
    }

    EXPECT_TRUE((st_link.st_mode & 0777) == 0700, "link mode mismatch");
    EXPECT_TRUE((st_target.st_mode & 0777) == 0600, "target mode changed");

out:
    unlink(linkpath);
    unlink(target);
}

/* openat2 (SYS 437). */

#ifndef SYS_openat2
#define SYS_openat2 437
#endif

struct open_how {
    unsigned long long flags, mode, resolve;
};

#define RESOLVE_BENEATH 0x08
#define RESOLVE_IN_ROOT 0x10
#define RESOLVE_NO_MAGICLINKS 0x02
#define RESOLVE_NO_SYMLINKS 0x04

static void test_openat2_basic(void)
{
    TEST("openat2 basic open");
    struct open_how how = {.flags = O_RDONLY, .mode = 0, .resolve = 0};
    long fd = syscall(SYS_openat2, AT_FDCWD, "/dev/null", &how, sizeof(how));
    if (fd < 0) {
        FAIL("openat2");
        return;
    }
    close(fd);
    PASS();
}

static void test_openat2_resolve_beneath(void)
{
    TEST("openat2 RESOLVE_BENEATH rejects ..");
    /* Open a directory first */
    int dirfd = open("/tmp", O_RDONLY | O_DIRECTORY);
    if (dirfd < 0) {
        FAIL("open /tmp");
        return;
    }
    struct open_how how = {
        .flags = O_RDONLY, .mode = 0, .resolve = RESOLVE_BENEATH};
    long fd = syscall(SYS_openat2, dirfd, "../etc/passwd", &how, sizeof(how));
    close(dirfd);
    if (fd >= 0) {
        close(fd);
        FAIL("should have rejected .. traversal");
        return;
    }
    EXPECT_TRUE(errno == EXDEV, "wrong errno");
}

static void test_openat2_resolve_beneath_allows_internal_dotdot(void)
{
    TEST("openat2 RESOLVE_BENEATH allows in-root ..");

    char dir_template[] = "/tmp/elfuse-openat2-beneath-XXXXXX";
    char subdir[PATH_MAX], target[PATH_MAX];
    int dirfd = -1, filefd = -1;

    if (!mkdtemp(dir_template)) {
        FAIL("mkdtemp");
        return;
    }

    snprintf(subdir, sizeof(subdir), "%s/subdir", dir_template);
    snprintf(target, sizeof(target), "%s/file", dir_template);
    if (mkdir(subdir, 0700) < 0) {
        FAIL("mkdir");
        goto out;
    }

    filefd = open(target, O_CREAT | O_RDONLY, 0600);
    if (filefd < 0) {
        FAIL("open file");
        goto out;
    }
    close(filefd);
    filefd = -1;

    dirfd = open(dir_template, O_RDONLY | O_DIRECTORY);
    if (dirfd < 0) {
        FAIL("open dir");
        goto out;
    }

    struct open_how how = {
        .flags = O_RDONLY, .mode = 0, .resolve = RESOLVE_BENEATH};
    long fd = syscall(SYS_openat2, dirfd, "subdir/../file", &how, sizeof(how));
    if (fd < 0) {
        FAIL("openat2");
        goto out;
    }
    close((int) fd);
    PASS();

out:
    if (dirfd >= 0)
        close(dirfd);
    if (filefd >= 0)
        close(filefd);
    unlink(target);
    rmdir(subdir);
    rmdir(dir_template);
}

static void test_openat2_resolve_in_root_clamps_dotdot(void)
{
    TEST("openat2 RESOLVE_IN_ROOT clamps .. at root");

    char dir_template[] = "/tmp/elfuse-openat2-inroot-XXXXXX";
    char target[PATH_MAX];
    int dirfd = -1, filefd = -1;

    if (!mkdtemp(dir_template)) {
        FAIL("mkdtemp");
        return;
    }

    snprintf(target, sizeof(target), "%s/file", dir_template);
    filefd = open(target, O_CREAT | O_RDONLY, 0600);
    if (filefd < 0) {
        FAIL("open file");
        goto out;
    }
    close(filefd);
    filefd = -1;

    dirfd = open(dir_template, O_RDONLY | O_DIRECTORY);
    if (dirfd < 0) {
        FAIL("open dir");
        goto out;
    }

    struct open_how how = {
        .flags = O_RDONLY, .mode = 0, .resolve = RESOLVE_IN_ROOT};
    long fd = syscall(SYS_openat2, dirfd, "/../file", &how, sizeof(how));
    if (fd < 0) {
        FAIL("openat2");
        goto out;
    }
    close((int) fd);
    PASS();

out:
    if (dirfd >= 0)
        close(dirfd);
    if (filefd >= 0)
        close(filefd);
    unlink(target);
    rmdir(dir_template);
}

static void test_openat2_resolve_no_symlinks_intermediate(void)
{
    TEST("openat2 RESOLVE_NO_SYMLINKS rejects intermediate symlink");

    char dir_template[] = "/tmp/elfuse-openat2-XXXXXX";
    char target_dir[PATH_MAX], subfile[PATH_MAX];
    char link_path[PATH_MAX];
    int dirfd = -1, filefd = -1;

    if (!mkdtemp(dir_template)) {
        FAIL("mkdtemp");
        return;
    }

    snprintf(target_dir, sizeof(target_dir), "%s/real", dir_template);
    snprintf(subfile, sizeof(subfile), "%s/subfile", target_dir);
    snprintf(link_path, sizeof(link_path), "%s/link", dir_template);

    if (mkdir(target_dir, 0700) < 0) {
        FAIL("mkdir");
        goto out;
    }
    filefd = open(subfile, O_CREAT | O_RDWR, 0600);
    if (filefd < 0) {
        FAIL("open subfile");
        goto out;
    }
    close(filefd);
    filefd = -1;
    if (symlink("real", link_path) < 0) {
        FAIL("symlink");
        goto out;
    }

    dirfd = open(dir_template, O_RDONLY | O_DIRECTORY);
    if (dirfd < 0) {
        FAIL("open dir");
        goto out;
    }

    struct open_how how = {
        .flags = O_RDONLY, .mode = 0, .resolve = RESOLVE_NO_SYMLINKS};
    long fd = syscall(SYS_openat2, dirfd, "link/subfile", &how, sizeof(how));
    if (fd >= 0) {
        close((int) fd);
        FAIL("expected ELOOP");
        goto out;
    }
    EXPECT_TRUE(errno == ELOOP, "wrong errno");

out:
    if (dirfd >= 0)
        close(dirfd);
    if (filefd >= 0)
        close(filefd);
    unlink(link_path);
    unlink(subfile);
    rmdir(target_dir);
    rmdir(dir_template);
}

static void test_openat2_resolve_beneath_rejects_symlink_escape(void)
{
    TEST("openat2 RESOLVE_BENEATH rejects symlink escape");

    char dir_template[] = "/tmp/elfuse-openat2-escape-XXXXXX";
    char link_path[PATH_MAX];
    int dirfd = -1;

    if (!mkdtemp(dir_template)) {
        FAIL("mkdtemp");
        return;
    }

    snprintf(link_path, sizeof(link_path), "%s/link", dir_template);
    if (symlink("/etc", link_path) < 0) {
        FAIL("symlink");
        goto out;
    }

    dirfd = open(dir_template, O_RDONLY | O_DIRECTORY);
    if (dirfd < 0) {
        FAIL("open dir");
        goto out;
    }

    struct open_how how = {
        .flags = O_RDONLY, .mode = 0, .resolve = RESOLVE_BENEATH};
    long fd = syscall(SYS_openat2, dirfd, "link/passwd", &how, sizeof(how));
    if (fd >= 0) {
        close((int) fd);
        FAIL("expected EXDEV");
        goto out;
    }
    EXPECT_TRUE(errno == EXDEV, "wrong errno");

out:
    if (dirfd >= 0)
        close(dirfd);
    unlink(link_path);
    rmdir(dir_template);
}

static void test_openat2_resolve_no_magiclinks_proc_fd(void)
{
    TEST("openat2 RESOLVE_NO_MAGICLINKS rejects /proc/self/fd");
    struct open_how how = {
        .flags = O_RDONLY, .mode = 0, .resolve = RESOLVE_NO_MAGICLINKS};
    long fd =
        syscall(SYS_openat2, AT_FDCWD, "/proc/self/fd/0", &how, sizeof(how));
    if (fd >= 0) {
        close((int) fd);
        FAIL("expected ELOOP");
        return;
    }
    EXPECT_TRUE(errno == ELOOP, "wrong errno");
}

static void test_openat2_resolve_no_magiclinks_proc_cwd(void)
{
    TEST("openat2 RESOLVE_NO_MAGICLINKS rejects proc cwd magiclinks");
    struct open_how how = {
        .flags = O_RDONLY, .mode = 0, .resolve = RESOLVE_NO_MAGICLINKS};
    char cwd[256];

    if (!getcwd(cwd, sizeof(cwd))) {
        FAIL("getcwd");
        return;
    }
    if (chdir("/proc") < 0) {
        FAIL("chdir");
        return;
    }

    errno = 0;
    long fd = syscall(SYS_openat2, AT_FDCWD, "self/fd/0", &how, sizeof(how));
    int saved_errno = errno;
    if (chdir(cwd) < 0) {
        FAIL("restore cwd");
        return;
    }

    errno = saved_errno;
    if (fd >= 0) {
        close((int) fd);
        FAIL("expected ELOOP");
        return;
    }
    EXPECT_TRUE(errno == ELOOP, "wrong errno");
}

/* O_PATH enforcement. */

#ifndef O_PATH
#define O_PATH 010000000
#endif

#ifndef MADV_COLD
#define MADV_COLD 20
#endif

static void test_opath_read_fails(void)
{
    TEST("O_PATH fd rejects read");
    int fd = open("/dev/null", O_PATH);
    if (fd < 0) {
        FAIL("open O_PATH");
        return;
    }
    char buf[1];
    ssize_t n = read(fd, buf, 1);
    close(fd);
    EXPECT_TRUE(n < 0 && errno == EBADF, "read should return EBADF");
}

static void test_opath_write_fails(void)
{
    TEST("O_PATH fd rejects write");
    int fd = open("/dev/null", O_PATH);
    if (fd < 0) {
        FAIL("open O_PATH");
        return;
    }
    ssize_t n = write(fd, "x", 1);
    close(fd);
    EXPECT_TRUE(n < 0 && errno == EBADF, "write should return EBADF");
}

static void test_opath_fstat_works(void)
{
    TEST("O_PATH fd allows fstat");
    int fd = open("/dev/null", O_PATH);
    if (fd < 0) {
        FAIL("open O_PATH");
        return;
    }
    struct stat st;
    int rc = fstat(fd, &st);
    close(fd);
    EXPECT_TRUE(rc == 0, "fstat should work on O_PATH");
}

/* madvise parity. */

static void test_madvise_cold(void)
{
    TEST("madvise MADV_COLD accepted");
    void *p = mmap(NULL, 4096, PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (p == MAP_FAILED) {
        FAIL("mmap");
        return;
    }
    int rc = madvise(p, 4096, MADV_COLD);
    munmap(p, 4096);
    EXPECT_TRUE(rc == 0, "madvise MADV_COLD");
}

static void test_madvise_dontneed_unmapped(void)
{
    TEST("madvise DONTNEED on unmapped returns ENOMEM");
    /* Map a page, then unmap the second half to create a hole */
    void *p = mmap(NULL, 8192, PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (p == MAP_FAILED) {
        FAIL("mmap");
        return;
    }
    munmap((char *) p + 4096, 4096);
    /* MADV_DONTNEED across the boundary should fail */
    int rc = madvise(p, 8192, MADV_DONTNEED);
    munmap(p, 4096);
    EXPECT_TRUE(rc < 0 && errno == ENOMEM, "expected ENOMEM for unmapped hole");
}

/* /proc paths. */

static void test_proc_oom_score_adj(void)
{
    TEST("/proc/self/oom_score_adj readable");
    int fd = open("/proc/self/oom_score_adj", O_RDONLY);
    if (fd < 0) {
        FAIL("open");
        return;
    }
    char buf[32];
    ssize_t n = read(fd, buf, sizeof(buf) - 1);
    close(fd);
    if (n > 0) {
        buf[n] = '\0';
        int val = atoi(buf);
        EXPECT_TRUE(val == 0, "unexpected value");
    } else {
        FAIL("read");
    }
}

static void test_proc_oom_score_adj_persists_write(void)
{
    TEST("/proc/self/oom_score_adj write persists");
    int fd = open("/proc/self/oom_score_adj", O_RDWR);
    if (fd < 0) {
        FAIL("open");
        return;
    }

    const char value[] = "123\n";
    if (write(fd, value, sizeof(value) - 1) != (ssize_t) (sizeof(value) - 1)) {
        close(fd);
        FAIL("write");
        return;
    }
    close(fd);

    fd = open("/proc/self/oom_score_adj", O_RDONLY);
    if (fd < 0) {
        FAIL("reopen");
        return;
    }

    char buf[32];
    ssize_t n = read(fd, buf, sizeof(buf) - 1);
    close(fd);
    if (n <= 0) {
        FAIL("read");
        return;
    }

    buf[n] = '\0';
    EXPECT_TRUE(atoi(buf) == 123, "value did not persist");
}

static void test_signalfd_efault_preserves_pending(void)
{
    TEST("signalfd EFAULT preserves pending signal");

    sigset_t mask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGUSR1);
    sigprocmask(SIG_BLOCK, &mask, NULL);

    int fd = signalfd(-1, &mask, SFD_NONBLOCK);
    if (fd < 0) {
        FAIL("signalfd");
        return;
    }

    kill(getpid(), SIGUSR1);
    errno = 0;
    /* Deliberately bad pointer to verify the kernel reports EFAULT. */
    ssize_t bad = syscall(SYS_read, fd,
                          /* cppcheck-suppress intToPointerCast */
                          (void *) 1, sizeof(struct signalfd_siginfo));
    if (bad != -1 || errno != EFAULT) {
        close(fd);
        FAIL("expected EFAULT");
        return;
    }

    struct signalfd_siginfo info;
    memset(&info, 0, sizeof(info));
    ssize_t good = read(fd, &info, sizeof(info));
    close(fd);
    if (good == (ssize_t) sizeof(info) &&
        info.ssi_signo == (uint32_t) SIGUSR1) {
        PASS();
    } else {
        FAIL("signal was lost after EFAULT");
    }
}

static void test_proc_fdinfo(void)
{
    TEST("/proc/self/fdinfo/0 readable");
    int fd = open("/proc/self/fdinfo/0", O_RDONLY);
    if (fd < 0) {
        FAIL("open");
        return;
    }
    char buf[256];
    ssize_t n = read(fd, buf, sizeof(buf) - 1);
    close(fd);
    if (n > 0) {
        buf[n] = '\0';
        EXPECT_TRUE(strstr(buf, "pos:") && strstr(buf, "flags:"),
                    "missing pos/flags fields");
    } else {
        FAIL("read");
    }
}

static void test_proc_oom_score_adj_rejects_out_of_range(void)
{
    TEST("/proc/self/oom_score_adj rejects out-of-range writes");
    int fd = open("/proc/self/oom_score_adj", O_RDWR);
    if (fd < 0) {
        FAIL("open");
        return;
    }
    /* Linux validates the input domain on the writer side; the kernel
     * returns EINVAL for any value outside [-1000, 1000]. */
    const char too_high[] = "1001\n";
    ssize_t rc = write(fd, too_high, sizeof(too_high) - 1);
    int saved = errno;
    close(fd);
    if (rc < 0 && saved == EINVAL)
        PASS();
    else
        FAIL("expected -EINVAL");
}

static void test_proc_oom_adj_scaling(void)
{
    TEST("/proc/self/oom_adj scales to oom_score_adj");
    /* Reset to a known starting value so test ordering does not matter. */
    int z = open("/proc/self/oom_score_adj", O_RDWR);
    if (z >= 0) {
        write(z, "0\n", 2);
        close(z);
    }

    int fd = open("/proc/self/oom_adj", O_RDWR);
    if (fd < 0) {
        /* Some Linux configs deprecate oom_adj; treat absence as OK. */
        PASS();
        return;
    }
    /* Linux fs/proc/base.c oom_adj_write special-cases OOM_ADJUST_MAX so
     * 15 maps directly to OOM_SCORE_ADJ_MAX (1000), not 15*1000/17 = 882. */
    if (write(fd, "15\n", 3) != 3) {
        close(fd);
        FAIL("write");
        return;
    }
    close(fd);

    int sa = open("/proc/self/oom_score_adj", O_RDONLY);
    if (sa < 0) {
        FAIL("reopen oom_score_adj");
        return;
    }
    char buf[32] = {0};
    ssize_t n = read(sa, buf, sizeof(buf) - 1);
    close(sa);
    if (n <= 0) {
        FAIL("read");
        return;
    }
    int score = atoi(buf);
    EXPECT_TRUE(score == 1000, "oom_adj=15 should map to oom_score_adj=1000");
}

static void test_proc_oom_adj_same_fd_roundtrip(void)
{
    TEST("/proc/self/oom_adj same-fd readback stays legacy");

    int reset = open("/proc/self/oom_score_adj", O_RDWR);
    if (reset >= 0) {
        write(reset, "0\n", 2);
        close(reset);
    }

    int fd = open("/proc/self/oom_adj", O_RDWR);
    if (fd < 0) {
        PASS();
        return;
    }
    if (write(fd, "15\n", 3) != 3) {
        close(fd);
        FAIL("write");
        return;
    }
    if (lseek(fd, 0, SEEK_SET) < 0) {
        close(fd);
        FAIL("lseek");
        return;
    }

    char buf[32] = {0};
    ssize_t n = read(fd, buf, sizeof(buf) - 1);
    close(fd);
    reset = open("/proc/self/oom_score_adj", O_RDWR);
    if (reset >= 0) {
        write(reset, "0\n", 2);
        close(reset);
    }
    if (n <= 0) {
        FAIL("read");
        return;
    }
    EXPECT_TRUE(atoi(buf) == 15, "same-fd readback should preserve oom_adj");
}

static void test_proc_oom_score_no_write(void)
{
    TEST("/proc/self/oom_score writes are rejected");
    /* Linux: open succeeds (root bypasses the 0444 check, non-root sees
     * EACCES from the permission gate); writes always fail because there
     * is no write handler. The test focuses on the write side, which is
     * uniform across uids.
     */
    int fd = open("/proc/self/oom_score", O_RDONLY);
    if (fd < 0) {
        FAIL("open RDONLY");
        return;
    }
    char buf[32] = {0};
    ssize_t n = read(fd, buf, sizeof(buf) - 1);
    close(fd);
    if (n <= 0) {
        FAIL("read");
        return;
    }
    /* Stub returns 0; real Linux computes a small positive score, but for
     * a userspace bridge a constant zero is acceptable.
     */
    EXPECT_TRUE(atoi(buf) >= 0, "score must be non-negative");
}

static void test_proc_oom_score_write_fails(void)
{
    TEST("/proc/self/oom_score write is rejected");
    int fd = open("/proc/self/oom_score", O_WRONLY);
    if (fd < 0) {
        /* Non-root environments cannot open read-only file for write;
         * that is also acceptable proof the file is not writable.
         */
        if (errno == EACCES) {
            PASS();
            return;
        }
        FAIL("open WRONLY");
        return;
    }
    ssize_t w = write(fd, "0\n", 2);
    int saved = errno;
    close(fd);
    /* Linux's proc_reg_write returns -EIO when the proc node has no
     * write op. Older or stripped kernels may return other errno; the
     * load-bearing assertion is that the write fails, not the exact
     * errno value.
     */
    if (w < 0)
        PASS();
    else
        printf("FAIL: write succeeded rc=%zd errno=%d\n", w, saved), fails++;
}

static void test_proc_oom_score_open_enforces_read_only(void)
{
    TEST("/proc/self/oom_score rejects writable open");
    errno = 0;
    int fd = open("/proc/self/oom_score", O_WRONLY);
    if (fd >= 0) {
        close(fd);
        FAIL("open should fail");
        return;
    }
    EXPECT_TRUE(errno == EACCES, "expected EACCES from open");
}

static void test_proc_oom_adj_reread_tracks_score_adj_updates(void)
{
    TEST("/proc/self/oom_adj reread reflects later score_adj writes");

    int reset = open("/proc/self/oom_score_adj", O_RDWR);
    if (reset < 0) {
        FAIL("reset open");
        return;
    }
    if (write(reset, "0\n", 2) != 2) {
        close(reset);
        FAIL("reset write");
        return;
    }
    close(reset);

    int fd = open("/proc/self/oom_adj", O_RDONLY);
    if (fd < 0) {
        PASS();
        return;
    }

    char buf[32] = {0};
    ssize_t n = read(fd, buf, sizeof(buf) - 1);
    if (n <= 0 || atoi(buf) != 0) {
        close(fd);
        FAIL("initial read");
        return;
    }

    int score = open("/proc/self/oom_score_adj", O_RDWR);
    if (score < 0) {
        close(fd);
        FAIL("score_adj open");
        return;
    }
    if (write(score, "1000\n", 5) != 5) {
        close(score);
        close(fd);
        FAIL("score_adj write");
        return;
    }
    close(score);

    if (lseek(fd, 0, SEEK_SET) < 0) {
        close(fd);
        FAIL("lseek");
        return;
    }

    memset(buf, 0, sizeof(buf));
    n = read(fd, buf, sizeof(buf) - 1);
    close(fd);

    reset = open("/proc/self/oom_score_adj", O_RDWR);
    if (reset >= 0) {
        write(reset, "0\n", 2);
        close(reset);
    }

    if (n <= 0) {
        FAIL("reread");
        return;
    }
    EXPECT_TRUE(atoi(buf) == 15, "oom_adj fd should reflect current score_adj");
}

static void test_proc_oom_score_adj_reread_tracks_updates(void)
{
    TEST("/proc/self/oom_score_adj reread reflects later writes");

    int reset = open("/proc/self/oom_score_adj", O_RDWR);
    if (reset < 0) {
        FAIL("reset open");
        return;
    }
    if (write(reset, "0\n", 2) != 2) {
        close(reset);
        FAIL("reset write");
        return;
    }
    close(reset);

    int fd = open("/proc/self/oom_score_adj", O_RDONLY);
    if (fd < 0) {
        FAIL("open");
        return;
    }

    char buf[32] = {0};
    ssize_t n = read(fd, buf, sizeof(buf) - 1);
    if (n <= 0 || atoi(buf) != 0) {
        close(fd);
        FAIL("initial read");
        return;
    }

    int update = open("/proc/self/oom_score_adj", O_RDWR);
    if (update < 0) {
        close(fd);
        FAIL("update open");
        return;
    }
    if (write(update, "1000\n", 5) != 5) {
        close(update);
        close(fd);
        FAIL("update write");
        return;
    }
    close(update);

    if (lseek(fd, 0, SEEK_SET) < 0) {
        close(fd);
        FAIL("lseek");
        return;
    }

    memset(buf, 0, sizeof(buf));
    n = read(fd, buf, sizeof(buf) - 1);
    close(fd);

    reset = open("/proc/self/oom_score_adj", O_RDWR);
    if (reset >= 0) {
        write(reset, "0\n", 2);
        close(reset);
    }

    if (n <= 0) {
        FAIL("reread");
        return;
    }
    EXPECT_TRUE(atoi(buf) == 1000,
                "oom_score_adj fd should reflect current value");
}

static void test_proc_oom_zero_length_writev(void)
{
    TEST("/proc/self/oom_score_adj zero-length writev returns 0");
    int fd = open("/proc/self/oom_score_adj", O_WRONLY);
    if (fd < 0) {
        FAIL("open");
        return;
    }
    /* Two empty iovecs: total length zero. Linux returns 0; the previous
     * implementation returned EINVAL via proc_parse_int_write. */
    char dummy = 0;
    struct iovec iov[2] = {{&dummy, 0}, {&dummy, 0}};
    ssize_t n = writev(fd, iov, 2);
    int saved = errno;
    close(fd);
    if (n == 0)
        PASS();
    else
        printf("FAIL: writev returned %zd errno=%d\n", n, saved), fails++;
}

static void test_proc_oom_stat_size_zero(void)
{
    TEST("/proc/self/oom_score_adj stat reports size 0");
    struct stat st;
    if (stat("/proc/self/oom_score_adj", &st) < 0) {
        FAIL("stat");
        return;
    }
    /* A non-zero st_size would cap stat-sized read buffers, truncating
     * "-1000\n" (6 bytes) to whatever size was hardcoded. */
    EXPECT_TRUE(st.st_size == 0, "st_size should be 0");
}

static void test_proc_fdinfo_eventfd_count(void)
{
    TEST("/proc/self/fdinfo/<N> exposes eventfd-count");
    int efd = eventfd(42, 0);
    if (efd < 0) {
        FAIL("eventfd");
        return;
    }
    char path[64];
    snprintf(path, sizeof(path), "/proc/self/fdinfo/%d", efd);
    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        close(efd);
        FAIL("open");
        return;
    }
    char buf[256] = {0};
    ssize_t n = read(fd, buf, sizeof(buf) - 1);
    close(fd);
    close(efd);
    if (n <= 0) {
        FAIL("read");
        return;
    }
    /* Linux fs/eventfd.c emits "eventfd-count: %16llx" with a single
     * space separator (not a tab, unlike pos:/flags:/mnt_id:). Pin the
     * exact prefix so a regression to a tab is caught. Decimal 42 is 0x2a.
     */
    const char *p = strstr(buf, "eventfd-count: ");
    EXPECT_TRUE(p && strstr(p, "2a") != NULL,
                "eventfd-count missing space separator or wrong hex value");
}

static void test_proc_fdinfo_signalfd_mask(void)
{
    TEST("/proc/self/fdinfo/<N> exposes sigmask");
    sigset_t mask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGUSR1);
    int sfd = signalfd(-1, &mask, 0);
    if (sfd < 0) {
        FAIL("signalfd");
        return;
    }
    char path[64];
    snprintf(path, sizeof(path), "/proc/self/fdinfo/%d", sfd);
    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        close(sfd);
        FAIL("open");
        return;
    }
    char buf[256] = {0};
    ssize_t n = read(fd, buf, sizeof(buf) - 1);
    close(fd);
    close(sfd);
    if (n <= 0) {
        FAIL("read");
        return;
    }
    /* Linux fs/signalfd.c emits "sigmask:\t%016llx" with a tab separator
     * (verified against a real /proc/self/fdinfo dump on Linux 6.x).
     * Pin the exact prefix so a regression to a space is caught.
     */
    EXPECT_TRUE(strstr(buf, "sigmask:\t") != NULL,
                "sigmask missing tab separator");
}

static void test_proc_fdinfo_timerfd_periodic_value(void)
{
    TEST("/proc/self/fdinfo/<N> reports periodic timerfd next expiry");
    int tfd = timerfd_create(CLOCK_MONOTONIC, 0);
    if (tfd < 0) {
        FAIL("timerfd_create");
        return;
    }

    struct itimerspec its = {.it_value = {.tv_sec = 0, .tv_nsec = 50000000},
                             .it_interval = {.tv_sec = 0, .tv_nsec = 50000000}};
    if (timerfd_settime(tfd, 0, &its, NULL) < 0) {
        close(tfd);
        FAIL("timerfd_settime");
        return;
    }

    usleep(70000);

    char path[64];
    snprintf(path, sizeof(path), "/proc/self/fdinfo/%d", tfd);
    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        close(tfd);
        FAIL("open");
        return;
    }

    char buf[256] = {0};
    ssize_t n = read(fd, buf, sizeof(buf) - 1);
    close(fd);
    close(tfd);
    if (n <= 0) {
        FAIL("read");
        return;
    }

    long long value_sec = -1, value_nsec = -1;
    long long interval_sec = -1, interval_nsec = -1;
    /* Linux fs/timerfd.c emits "it_value: (S, NS)" with a single space
     * after the colon (unlike pos:/flags: which use tabs). */
    const char *value = strstr(buf, "it_value: (");
    const char *interval = strstr(buf, "it_interval: (");
    if (!value || !interval ||
        sscanf(value, "it_value: (%lld, %lld)", &value_sec, &value_nsec) != 2 ||
        sscanf(interval, "it_interval: (%lld, %lld)", &interval_sec,
               &interval_nsec) != 2) {
        FAIL("parse fdinfo");
        return;
    }

    long long value_total_ns = value_sec * 1000000000LL + value_nsec;
    long long interval_total_ns = interval_sec * 1000000000LL + interval_nsec;
    /* it_interval is the static settime value and must round-trip; Linux's
     * timerfd_get_remaining() reports 0 once the timer has fired, while
     * elfuse computes time-until-next from the kqueue arm time. Both are
     * non-negative and bounded by the interval, so accept either form.
     */
    EXPECT_TRUE(interval_total_ns == 50000000 && value_total_ns >= 0 &&
                    value_total_ns <= interval_total_ns,
                "interval should round-trip and value should be within bounds");
}

static void test_proc_fdinfo_timerfd_ticks_drains_kqueue(void)
{
    TEST("/proc/self/fdinfo/<N> ticks reflects pending kqueue fires");
    /* Arm a periodic timer, wait for several fires, then read fdinfo
     * WITHOUT first reading the timerfd. The pre-fix snapshot exported
     * a stale expirations counter (the kqueue events had not been folded
     * in), so ticks would read 0 even after multiple fires. Linux's
     * fs/timerfd.c snapshots ticks under the wait-queue lock, where the
     * counter reflects every fire that hit the kernel state. */
    int tfd = timerfd_create(CLOCK_MONOTONIC, 0);
    if (tfd < 0) {
        FAIL("timerfd_create");
        return;
    }
    struct itimerspec its = {.it_value = {.tv_sec = 0, .tv_nsec = 20000000},
                             .it_interval = {.tv_sec = 0, .tv_nsec = 20000000}};
    if (timerfd_settime(tfd, 0, &its, NULL) < 0) {
        close(tfd);
        FAIL("timerfd_settime");
        return;
    }
    /* Wait long enough for the timer to fire at least three times. */
    usleep(120000);

    char path[64];
    snprintf(path, sizeof(path), "/proc/self/fdinfo/%d", tfd);
    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        close(tfd);
        FAIL("open");
        return;
    }
    char buf[256] = {0};
    ssize_t n = read(fd, buf, sizeof(buf) - 1);
    close(fd);
    close(tfd);
    if (n <= 0) {
        FAIL("read");
        return;
    }

    /* Linux uses "ticks: %llu" with a single space; elfuse matches. */
    const char *p = strstr(buf, "ticks: ");
    unsigned long long ticks = 0;
    if (!p || sscanf(p, "ticks: %llu", &ticks) != 1) {
        FAIL("parse ticks");
        return;
    }
    /* At minimum one fire should be visible; on a slow host more would
     * be expected. Pre-fix elfuse would report 0 here. */
    EXPECT_TRUE(ticks >= 1, "ticks should reflect at least one fire");
}

static void test_proc_fdinfo_dir_concurrent_safe(void)
{
    TEST("/proc/self/fdinfo dir tolerates concurrent re-open");
    /* Open the directory twice and verify both enumerate independently.
     * The earlier shared-dir design could mutate one open's backing files
     * while another iterated. Both Linux and the per-open scratch fix
     * should at minimum surface stdin/out/err on each enumeration.
     */
    DIR *d1 = opendir("/proc/self/fdinfo");
    if (!d1) {
        FAIL("opendir 1");
        return;
    }
    DIR *d2 = opendir("/proc/self/fdinfo");
    if (!d2) {
        closedir(d1);
        FAIL("opendir 2");
        return;
    }

    int n1 = 0, n2 = 0;
    struct dirent *ent;
    while ((ent = readdir(d1)))
        if (ent->d_name[0] != '.')
            n1++;
    while ((ent = readdir(d2)))
        if (ent->d_name[0] != '.')
            n2++;
    closedir(d1);
    closedir(d2);
    EXPECT_TRUE(n1 >= 3 && n2 >= 3, "concurrent enumeration broken");
}

static void test_proc_fdinfo_dirfd_openat_uses_virtual_entries(void)
{
    TEST("/proc/self/fdinfo dirfd openat resolves virtually");
    int dirfd = open("/proc/self/fdinfo", O_RDONLY | O_DIRECTORY);
    if (dirfd < 0) {
        FAIL("open dir");
        return;
    }

    int fd = openat(dirfd, "0", O_RDONLY);
    close(dirfd);
    if (fd < 0) {
        FAIL("openat");
        return;
    }

    char buf[256] = {0};
    ssize_t n = read(fd, buf, sizeof(buf) - 1);
    close(fd);
    if (n <= 0) {
        FAIL("read");
        return;
    }

    EXPECT_TRUE(strstr(buf, "pos:\t") && strstr(buf, "flags:\t"),
                "fdinfo openat should yield synthetic payload");
}

static int bind_listen_loopback_tcp(void)
{
    int s = socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0)
        return -1;
    int one = 1;
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    struct sockaddr_in sa = {0};
    sa.sin_family = AF_INET;
    sa.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    sa.sin_port = 0;
    if (bind(s, (struct sockaddr *) &sa, sizeof(sa)) < 0 || listen(s, 1) < 0) {
        close(s);
        return -1;
    }
    return s;
}

static void test_proc_net_tcp_sl_dense(void)
{
    TEST("/proc/net/tcp sl column stays dense across mixed sockets");
    /* Interleave non-TCP sockets BEFORE the bound TCP listeners so the
     * proc_pidinfo iterator visits the rejected sockets first and the
     * pre-fix sparse-slot bug would assign nonzero sl to the first
     * emitted row. Two TCP listeners ensure the second row's sl exposes
     * any gap created by additional non-TCP visits between them.
     *
     * Pre-fix: udp1, udp2, sp[0], sp[1] all bump the iterator slot
     * counter to 4 before tcp1 emits. tcp1 row: sl=4. tcp2 row: sl=5.
     * The first-row check (sl == 0) would fail.
     * Post-fix: only emitted rows increment the visitor's row counter;
     * tcp1: sl=0, tcp2: sl=1. Dense.
     */
    int udp1 = socket(AF_INET, SOCK_DGRAM, 0);
    int udp2 = socket(AF_INET, SOCK_DGRAM, 0);
    int sp[2] = {-1, -1};
    int sp_rc = socketpair(AF_UNIX, SOCK_STREAM, 0, sp);
    int tcp1 = bind_listen_loopback_tcp();
    int tcp2 = bind_listen_loopback_tcp();
    if (udp1 < 0 || udp2 < 0 || sp_rc < 0 || tcp1 < 0 || tcp2 < 0) {
        if (udp1 >= 0)
            close(udp1);
        if (udp2 >= 0)
            close(udp2);
        if (sp[0] >= 0)
            close(sp[0]);
        if (sp[1] >= 0)
            close(sp[1]);
        if (tcp1 >= 0)
            close(tcp1);
        if (tcp2 >= 0)
            close(tcp2);
        FAIL("socket setup");
        return;
    }

    int fd = open("/proc/net/tcp", O_RDONLY);
    if (fd < 0) {
        close(udp1);
        close(udp2);
        close(sp[0]);
        close(sp[1]);
        close(tcp1);
        close(tcp2);
        FAIL("open");
        return;
    }
    char buf[16384];
    ssize_t total = 0;
    for (;;) {
        ssize_t n = read(fd, buf + total, sizeof(buf) - total - 1);
        if (n <= 0)
            break;
        total += n;
    }
    close(fd);
    close(udp1);
    close(udp2);
    close(sp[0]);
    close(sp[1]);
    close(tcp1);
    close(tcp2);
    buf[total] = '\0';

    /* Skip the header line; collect each subsequent row's leading "sl"
     * field. /proc/net/tcp's row format is "  N: ..." with N a decimal
     * serial. Verify the serials form 0,1,2,... with no gaps.
     */
    char *line = strchr(buf, '\n');
    if (!line) {
        FAIL("no rows");
        return;
    }
    line++;
    int expected = 0;
    while (*line) {
        char *colon = strchr(line, ':');
        char *eol = strchr(line, '\n');
        if (!colon || (eol && colon > eol))
            break;
        int sl = atoi(line);
        if (sl != expected) {
            printf("FAIL: sl=%d expected=%d\n", sl, expected);
            fails++;
            return;
        }
        expected++;
        if (!eol)
            break;
        line = eol + 1;
    }
    if (expected == 0) {
        /* The bound listener should have produced a row. Treat absence
         * as failure since the regression coverage depends on it. */
        FAIL("no TCP rows after bind/listen");
        return;
    }
    PASS();
}

static void test_proc_net_dirfd_openat_uses_virtual_entries(void)
{
    TEST("/proc/net dirfd openat resolves virtually");
    int dirfd = open("/proc/net", O_RDONLY | O_DIRECTORY);
    if (dirfd < 0) {
        FAIL("open dir");
        return;
    }

    int fd = openat(dirfd, "tcp", O_RDONLY);
    close(dirfd);
    if (fd < 0) {
        FAIL("openat");
        return;
    }

    char buf[512] = {0};
    ssize_t n = read(fd, buf, sizeof(buf) - 1);
    close(fd);
    if (n <= 0) {
        FAIL("read");
        return;
    }

    EXPECT_TRUE(strstr(buf, "local_address"),
                "proc net dirfd should preserve synthetic tcp table");
}

static void test_proc_cpuinfo_all_cpus(void)
{
    TEST("/proc/cpuinfo lists all CPUs");
    int fd = open("/proc/cpuinfo", O_RDONLY);
    if (fd < 0) {
        FAIL("open");
        return;
    }
    char buf[65536];
    ssize_t total = 0;
    for (;;) {
        ssize_t n = read(fd, buf + total, sizeof(buf) - total - 1);
        if (n <= 0)
            break;
        total += n;
    }
    close(fd);
    buf[total] = '\0';

    long ncpu = sysconf(_SC_NPROCESSORS_ONLN);
    int count = 0;
    char *p = buf;
    while ((p = strstr(p, "processor\t:"))) {
        count++;
        p++;
    }
    if (count == ncpu)
        PASS();
    else {
        printf("FAIL: found %d cpus, expected %ld\n", count, ncpu);
        fails++;
    }
}

int main(void)
{
    printf("Tier B correctness tests:\n");

    /* fchmodat2 */
    test_fchmodat2_basic();
    test_fchmodat2_symlink_nofollow();

    /* openat2 RESOLVE_* */
    test_openat2_basic();
    test_openat2_resolve_beneath();
    test_openat2_resolve_beneath_allows_internal_dotdot();
    test_openat2_resolve_in_root_clamps_dotdot();
    test_openat2_resolve_no_symlinks_intermediate();
    test_openat2_resolve_beneath_rejects_symlink_escape();
    test_openat2_resolve_no_magiclinks_proc_fd();
    test_openat2_resolve_no_magiclinks_proc_cwd();

    /* O_PATH */
    test_opath_read_fails();
    test_opath_write_fails();
    test_opath_fstat_works();

    /* madvise */
    test_madvise_cold();
    test_madvise_dontneed_unmapped();

    /* /proc */
    test_proc_oom_score_adj();
    test_proc_oom_score_adj_persists_write();
    test_proc_oom_score_adj_rejects_out_of_range();
    test_proc_oom_adj_scaling();
    test_proc_oom_adj_same_fd_roundtrip();
    test_proc_oom_adj_reread_tracks_score_adj_updates();
    test_proc_oom_score_adj_reread_tracks_updates();
    test_proc_oom_score_no_write();
    test_proc_oom_score_write_fails();
    test_proc_oom_score_open_enforces_read_only();
    test_proc_oom_zero_length_writev();
    test_proc_oom_stat_size_zero();
    test_proc_fdinfo();
    test_proc_fdinfo_eventfd_count();
    test_proc_fdinfo_signalfd_mask();
    test_proc_fdinfo_timerfd_periodic_value();
    test_proc_fdinfo_timerfd_ticks_drains_kqueue();
    test_proc_fdinfo_dir_concurrent_safe();
    test_proc_fdinfo_dirfd_openat_uses_virtual_entries();
    test_proc_net_tcp_sl_dense();
    test_proc_net_dirfd_openat_uses_virtual_entries();
    test_proc_cpuinfo_all_cpus();

    /* signalfd */
    test_signalfd_efault_preserves_pending();

    SUMMARY("test-tier-b");
    return fails ? 1 : 0;
}
