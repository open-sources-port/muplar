/* Test eventfd2 syscall emulation
 *
 * Copyright 2026 elfuse contributors
 * Copyright 2025 Moritz Angermann, zw3rk pte. ltd.
 * SPDX-License-Identifier: Apache-2.0
 *
 * Tests: eventfd create with flags, counter read/write semantics,
 *        EFD_SEMAPHORE mode, EFD_NONBLOCK EAGAIN, poll readiness
 *
 * Syscalls exercised: eventfd2(19), read(63), write(64), poll/ppoll(73),
 *                     close(57)
 */

#include <stdbool.h>
#include <stdint.h>
#include <unistd.h>
#include <poll.h>
#include <sys/eventfd.h>

#include "test-harness.h"

int main(void)
{
    int passes = 0, fails = 0;

    printf("test-eventfd: eventfd2 emulation tests\n");

    /* Test eventfd create with CLOEXEC + NONBLOCK flags */
    TEST("eventfd(EFD_CLOEXEC|NONBLOCK)");
    {
        int fd = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
        EXPECT_TRUE(fd >= 0, "eventfd create failed");
        close(fd);
    }

    /* Test write + read counter semantics (non-semaphore) */
    TEST("write+read counter");
    {
        int fd = eventfd(0, EFD_NONBLOCK);
        if (fd >= 0) {
            uint64_t val = 5;
            ssize_t w = write(fd, &val, sizeof(val));
            if (w == 8) {
                uint64_t out = 0;
                ssize_t r = read(fd, &out, sizeof(out));
                EXPECT_TRUE(r == 8 && out == 5, "counter mismatch");
            } else
                FAIL("write failed");
            close(fd);
        } else
            FAIL("eventfd create failed");
    }

    /* Test counter accumulation: two writes, one read returns sum */
    TEST("counter accumulation");
    {
        int fd = eventfd(0, EFD_NONBLOCK);
        if (fd >= 0) {
            uint64_t v1 = 3, v2 = 7;
            if (write(fd, &v1, sizeof(v1)) != 8 ||
                write(fd, &v2, sizeof(v2)) != 8) {
                FAIL("write failed");
                close(fd);
                goto accum_done;
            }
            uint64_t out = 0;
            ssize_t r = read(fd, &out, sizeof(out));
            EXPECT_TRUE(r == 8 && out == 10, "accumulation wrong");
            close(fd);
        } else
            FAIL("eventfd create failed");
    }
accum_done:

    /* Test EFD_SEMAPHORE: write 3, read returns 1 three times, then EAGAIN */
    TEST("EFD_SEMAPHORE");
    {
        int fd = eventfd(0, EFD_SEMAPHORE | EFD_NONBLOCK);
        if (fd >= 0) {
            uint64_t val = 3;
            write(fd, &val, sizeof(val));
            bool ok = true;
            for (int i = 0; i < 3; i++) {
                uint64_t out = 0;
                ssize_t r = read(fd, &out, sizeof(out));
                if (r != 8 || out != 1) {
                    ok = false;
                    break;
                }
            }
            /* Fourth read should fail with EAGAIN */
            uint64_t out = 0;
            ssize_t r = read(fd, &out, sizeof(out));
            EXPECT_TRUE(ok && r == -1 && errno == EAGAIN,
                        "semaphore semantics wrong");
            close(fd);
        } else
            FAIL("eventfd create failed");
    }

    /* Test EAGAIN on empty eventfd (nonblocking) */
    TEST("EAGAIN on empty");
    {
        int fd = eventfd(0, EFD_NONBLOCK);
        if (fd >= 0) {
            uint64_t out = 0;
            EXPECT_ERRNO(read(fd, &out, sizeof(out)), EAGAIN,
                         "expected EAGAIN");
            close(fd);
        } else
            FAIL("eventfd create failed");
    }

    /* Test poll: POLLIN should be set after write */
    TEST("poll(POLLIN after write)");
    {
        int fd = eventfd(0, EFD_NONBLOCK);
        if (fd >= 0) {
            uint64_t val = 1;
            write(fd, &val, sizeof(val));
            struct pollfd pfd = {.fd = fd, .events = POLLIN};
            int ret = poll(&pfd, 1, 0);
            EXPECT_TRUE(ret > 0 && (pfd.revents & POLLIN),
                        "poll did not report POLLIN");
            close(fd);
        } else
            FAIL("eventfd create failed");
    }

    /* Test initial value is readable immediately */
    TEST("eventfd(initval=42)");
    {
        int fd = eventfd(42, EFD_NONBLOCK);
        if (fd >= 0) {
            uint64_t out = 0;
            ssize_t r = read(fd, &out, sizeof(out));
            EXPECT_TRUE(r == 8 && out == 42, "initial value wrong");
            close(fd);
        } else
            FAIL("eventfd create failed");
    }

    /* Unknown eventfd flags must be rejected instead of silently ignored. */
    TEST("eventfd invalid flags");
    EXPECT_ERRNO(eventfd(0, 0x40000000), EINVAL, "expected EINVAL");

    SUMMARY("test-eventfd");
    return fails > 0 ? 1 : 0;
}
