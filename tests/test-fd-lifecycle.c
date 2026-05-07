/* fd metadata lifecycle regression tests
 *
 * Copyright 2026 elfuse contributors
 * Copyright 2025 Moritz Angermann, zw3rk pte. ltd.
 * SPDX-License-Identifier: Apache-2.0
 *
 * Syscalls: memfd_create, fcntl, dup, close, openat, write, setrlimit
 */

#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <sys/resource.h>
#include <sys/syscall.h>
#include <unistd.h>

#include "test-harness.h"

#ifndef MFD_ALLOW_SEALING
#define MFD_ALLOW_SEALING 0x0002U
#endif

#ifndef SYS_memfd_create
#define SYS_memfd_create 279
#endif

#ifndef F_ADD_SEALS
#define F_ADD_SEALS 1033
#endif

#ifndef F_SEAL_WRITE
#define F_SEAL_WRITE 0x0008
#endif

int passes = 0, fails = 0;

static int create_memfd(void)
{
    return (int) syscall(SYS_memfd_create, "elfuse-seal-test",
                         MFD_ALLOW_SEALING);
}

static void test_memfd_seals_survive_dup(void)
{
    TEST("memfd seals copied to dup");

    int fd = create_memfd();
    if (fd < 0) {
        FAIL("memfd_create failed");
        return;
    }

    if (fcntl(fd, F_ADD_SEALS, F_SEAL_WRITE) < 0) {
        FAIL("F_ADD_SEALS failed");
        close(fd);
        return;
    }

    int dupfd = dup(fd);
    if (dupfd < 0) {
        FAIL("dup failed");
        close(fd);
        return;
    }

    char c = 'x';
    errno = 0;
    EXPECT_ERRNO(write(dupfd, &c, 1), EPERM, "sealed dup accepted write");

    close(dupfd);
    close(fd);
}

static void test_memfd_seals_propagate_to_dup(void)
{
    TEST("memfd seals propagate to dup");

    int fd = create_memfd();
    if (fd < 0) {
        FAIL("memfd_create failed");
        return;
    }

    int dupfd = dup(fd);
    if (dupfd < 0) {
        FAIL("dup failed");
        close(fd);
        return;
    }

    if (fcntl(fd, F_ADD_SEALS, F_SEAL_WRITE) < 0) {
        FAIL("F_ADD_SEALS failed");
        close(dupfd);
        close(fd);
        return;
    }

    char c = 'x';
    errno = 0;
    EXPECT_ERRNO(write(dupfd, &c, 1), EPERM,
                 "post-dup seal did not affect dup");

    close(dupfd);
    close(fd);
}

static void test_memfd_seals_cleared_on_reuse(void)
{
    TEST("fd seals cleared on reuse");

    int fd = create_memfd();
    if (fd < 0) {
        FAIL("memfd_create failed");
        return;
    }
    if (fcntl(fd, F_ADD_SEALS, F_SEAL_WRITE) < 0) {
        FAIL("F_ADD_SEALS failed");
        close(fd);
        return;
    }

    close(fd);

    char path[] = "/tmp/elfuse-seal-reuse-XXXXXX";
    int regular = mkstemp(path);
    if (regular < 0) {
        FAIL("mkstemp failed");
        return;
    }
    unlink(path);

    char c = 'y';
    errno = 0;
    ssize_t ret = write(regular, &c, 1);
    EXPECT_TRUE(ret == 1, "reused fd kept stale seal");

    close(regular);
}

static void test_rlimit_nofile_reports_emfile(void)
{
    TEST("RLIMIT_NOFILE reports EMFILE");

    struct rlimit old_limit;
    if (getrlimit(RLIMIT_NOFILE, &old_limit) != 0) {
        FAIL("getrlimit failed");
        return;
    }

    int held = open("/dev/null", O_RDONLY);
    if (held < 0) {
        FAIL("open setup failed");
        return;
    }

    struct rlimit new_limit = old_limit;
    new_limit.rlim_cur = (rlim_t) (held + 1);
    if (setrlimit(RLIMIT_NOFILE, &new_limit) != 0) {
        FAIL("setrlimit failed");
        close(held);
        return;
    }

    errno = 0;
    int fd = open("/dev/null", O_RDONLY);
    if (fd < 0 && errno == EMFILE)
        PASS();
    else {
        if (fd >= 0)
            close(fd);
        FAIL("open did not return EMFILE");
    }

    setrlimit(RLIMIT_NOFILE, &old_limit);
    close(held);
}

static void test_dup3_above_rlimit_fails(void)
{
    TEST("dup3 above RLIMIT_NOFILE fails");

    struct rlimit old_limit;
    if (getrlimit(RLIMIT_NOFILE, &old_limit) != 0) {
        FAIL("getrlimit failed");
        return;
    }

    int pipefd[2];
    if (pipe(pipefd) != 0) {
        FAIL("pipe setup failed");
        return;
    }

    struct rlimit new_limit = old_limit;
    new_limit.rlim_cur = (rlim_t) (pipefd[1] + 1);
    if (setrlimit(RLIMIT_NOFILE, &new_limit) != 0) {
        FAIL("setrlimit failed");
        close(pipefd[0]);
        close(pipefd[1]);
        return;
    }

    errno = 0;
    int dupfd = dup3(pipefd[0], pipefd[1] + 1, 0);
    EXPECT_TRUE(dupfd < 0 && (errno == EBADF || errno == EMFILE),
                "dup3 beyond RLIMIT_NOFILE should fail with EBADF or EMFILE");

    setrlimit(RLIMIT_NOFILE, &old_limit);
    close(pipefd[0]);
    close(pipefd[1]);
}

int main(void)
{
    printf("test-fd-lifecycle: fd metadata lifecycle tests\n\n");

    test_memfd_seals_survive_dup();
    test_memfd_seals_propagate_to_dup();
    test_memfd_seals_cleared_on_reuse();
    test_rlimit_nofile_reports_emfile();
    test_dup3_above_rlimit_fails();

    SUMMARY("test-fd-lifecycle");
    return fails > 0 ? 1 : 0;
}
