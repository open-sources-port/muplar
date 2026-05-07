/* Test epoll emulation (kqueue backend)
 *
 * Copyright 2026 elfuse contributors
 * Copyright 2025 Moritz Angermann, zw3rk pte. ltd.
 * SPDX-License-Identifier: Apache-2.0
 *
 * Tests: epoll_create1, epoll_ctl (ADD/MOD/DEL), epoll_wait with
 *        pipes and eventfds, data.u64 preservation, timeout behavior
 *
 * Syscalls exercised: epoll_create1(20), epoll_ctl(21), epoll_pwait(22),
 *                     pipe2(59), eventfd2(19), write(64), close(57)
 */

#include <stdint.h>
#include <errno.h>
#include <unistd.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <sys/syscall.h>
#include <time.h>

#include "test-harness.h"

#ifndef __NR_epoll_pwait2
#define __NR_epoll_pwait2 441
#endif

int main(void)
{
    int passes = 0, fails = 0;

    printf("test-epoll: epoll emulation tests\n");

    /* Test epoll_create1 with CLOEXEC */
    TEST("epoll_create1(CLOEXEC)");
    {
        int epfd = epoll_create1(EPOLL_CLOEXEC);
        EXPECT_TRUE(epfd >= 0, "epoll_create1 failed");
        close(epfd);
    }

    /* Test EPOLL_CTL_ADD + epoll_wait: pipe read end becomes ready */
    TEST("ADD pipe + wait EPOLLIN");
    {
        int epfd = epoll_create1(0);
        int pipefd[2];
        if (pipe(pipefd) < 0) {
            FAIL("pipe");
            pipefd[0] = pipefd[1] = -1;
        }

        struct epoll_event ev = {.events = EPOLLIN, .data.fd = pipefd[0]};
        if (epoll_ctl(epfd, EPOLL_CTL_ADD, pipefd[0], &ev) == 0) {
            write(pipefd[1], "x", 1);

            struct epoll_event out;
            int n = epoll_wait(epfd, &out, 1, 100);
            EXPECT_TRUE(
                n == 1 && (out.events & EPOLLIN) && out.data.fd == pipefd[0],
                "epoll_wait wrong result");
        } else
            FAIL("epoll_ctl ADD failed");

        close(pipefd[0]);
        close(pipefd[1]);
        close(epfd);
    }

    /* Test EPOLLOUT on pipe write end (always writable) */
    TEST("ADD pipe write + EPOLLOUT");
    {
        int epfd = epoll_create1(0);
        int pipefd[2];
        if (pipe(pipefd) < 0) {
            FAIL("pipe");
            pipefd[0] = pipefd[1] = -1;
        }

        struct epoll_event ev = {.events = EPOLLOUT, .data.fd = pipefd[1]};
        if (epoll_ctl(epfd, EPOLL_CTL_ADD, pipefd[1], &ev) == 0) {
            struct epoll_event out;
            int n = epoll_wait(epfd, &out, 1, 100);
            EXPECT_TRUE(n == 1 && (out.events & EPOLLOUT),
                        "pipe not write-ready");
        } else
            FAIL("epoll_ctl ADD failed");

        close(pipefd[0]);
        close(pipefd[1]);
        close(epfd);
    }

    /* Test EPOLL_CTL_MOD: change monitored events */
    TEST("CTL_MOD events");
    {
        int epfd = epoll_create1(0);
        int pipefd[2];
        if (pipe(pipefd) < 0) {
            FAIL("pipe");
            pipefd[0] = pipefd[1] = -1;
        }

        /* Add read end for EPOLLIN */
        struct epoll_event ev = {.events = EPOLLIN, .data.fd = pipefd[0]};
        epoll_ctl(epfd, EPOLL_CTL_ADD, pipefd[0], &ev);

        /* Modify to EPOLLOUT; pipe read end cannot write, so no event */
        ev.events = EPOLLOUT;
        if (epoll_ctl(epfd, EPOLL_CTL_MOD, pipefd[0], &ev) == 0) {
            struct epoll_event out;
            int n = epoll_wait(epfd, &out, 1, 10);
            EXPECT_TRUE(n == 0, "unexpected event after MOD");
        } else
            FAIL("epoll_ctl MOD failed");

        close(pipefd[0]);
        close(pipefd[1]);
        close(epfd);
    }

    /* Test EPOLL_CTL_DEL: events stop after removal */
    TEST("CTL_DEL removes fd");
    {
        int epfd = epoll_create1(0);
        int pipefd[2];
        if (pipe(pipefd) < 0) {
            FAIL("pipe");
            pipefd[0] = pipefd[1] = -1;
        }

        struct epoll_event ev = {.events = EPOLLIN, .data.fd = pipefd[0]};
        epoll_ctl(epfd, EPOLL_CTL_ADD, pipefd[0], &ev);

        if (epoll_ctl(epfd, EPOLL_CTL_DEL, pipefd[0], NULL) == 0) {
            write(pipefd[1], "x", 1);
            struct epoll_event out;
            int n = epoll_wait(epfd, &out, 1, 10);
            EXPECT_TRUE(n == 0, "event after DEL");
        } else
            FAIL("epoll_ctl DEL failed");

        close(pipefd[0]);
        close(pipefd[1]);
        close(epfd);
    }

    /* Test EPOLL_CTL_DEL on an unregistered fd.
     * Repeat the failure path enough times to catch leaked host dup refs.
     */
    TEST("CTL_DEL ENOENT does not leak refs");
    {
        int epfd = epoll_create1(0);
        int pipefd[2];
        if (pipe(pipefd) < 0) {
            FAIL("pipe");
            pipefd[0] = pipefd[1] = -1;
        }
        int ok = 1;

        for (int i = 0; i < 4096; i++) {
            errno = 0;
            if (epoll_ctl(epfd, EPOLL_CTL_DEL, pipefd[0], NULL) != -1 ||
                errno != ENOENT) {
                ok = 0;
                break;
            }
        }

        EXPECT_TRUE(ok, "unexpected result from repeated DEL ENOENT");

        close(pipefd[0]);
        close(pipefd[1]);
        close(epfd);
    }

    /* Test timeout=0: non-blocking poll returns immediately */
    TEST("timeout=0 no events");
    {
        int epfd = epoll_create1(0);
        struct epoll_event out;
        int n = epoll_wait(epfd, &out, 1, 0);
        EXPECT_TRUE(n == 0, "expected 0 events");
        close(epfd);
    }

    /* Test epoll_pwait2 timeout=0 path */
    TEST("epoll_pwait2 timeout=0 no events");
    {
        int epfd = epoll_create1(0);
        struct epoll_event out;
        struct timespec ts = {.tv_sec = 0, .tv_nsec = 0};
        int n = syscall(__NR_epoll_pwait2, epfd, &out, 1, &ts, NULL, 8);
        EXPECT_TRUE(n == 0, "expected 0 events");
        close(epfd);
    }

    /* Test multiple FDs in same epoll instance */
    TEST("multiple fds (pipe+eventfd)");
    {
        int epfd = epoll_create1(0);
        int pipefd[2];
        if (pipe(pipefd) < 0) {
            FAIL("pipe");
            pipefd[0] = pipefd[1] = -1;
        }
        int efd = eventfd(0, EFD_NONBLOCK);

        struct epoll_event ev1 = {.events = EPOLLIN, .data.fd = pipefd[0]};
        struct epoll_event ev2 = {.events = EPOLLIN, .data.fd = efd};
        epoll_ctl(epfd, EPOLL_CTL_ADD, pipefd[0], &ev1);
        epoll_ctl(epfd, EPOLL_CTL_ADD, efd, &ev2);

        /* Make both readable */
        write(pipefd[1], "x", 1);
        uint64_t val = 1;
        write(efd, &val, sizeof(val));

        struct epoll_event out[4];
        int n = epoll_wait(epfd, out, 4, 100);
        EXPECT_TRUE(n >= 1, "no events from multiple fds");

        close(efd);
        close(pipefd[0]);
        close(pipefd[1]);
        close(epfd);
    }

    /* Test epoll_data.u64 is preserved through epoll_wait */
    TEST("epoll_data.u64 preserved");
    {
        int epfd = epoll_create1(0);
        int pipefd[2];
        if (pipe(pipefd) < 0) {
            FAIL("pipe");
            pipefd[0] = pipefd[1] = -1;
        }

        struct epoll_event ev = {.events = EPOLLIN,
                                 .data.u64 = 0xDEADBEEFCAFEULL};
        epoll_ctl(epfd, EPOLL_CTL_ADD, pipefd[0], &ev);
        write(pipefd[1], "x", 1);

        struct epoll_event out;
        int n = epoll_wait(epfd, &out, 1, 100);
        EXPECT_TRUE(n == 1 && out.data.u64 == 0xDEADBEEFCAFEULL,
                    "data not preserved");

        close(pipefd[0]);
        close(pipefd[1]);
        close(epfd);
    }

    /* Test EPOLLONESHOT + EPOLL_CTL_MOD re-arm
     * This is the critical test: after EPOLLONESHOT fires and is reported,
     * EPOLL_CTL_MOD should succeed to re-arm the registration.
     */
    TEST("EPOLLONESHOT re-arm with MOD");
    {
        int epfd = epoll_create1(0);
        int pipefd[2];
        if (pipe(pipefd) < 0) {
            FAIL("pipe");
            pipefd[0] = pipefd[1] = -1;
        }

        /* Register with EPOLLONESHOT */
        struct epoll_event ev = {.events = EPOLLIN | EPOLLONESHOT,
                                 .data.fd = pipefd[0]};
        if (epoll_ctl(epfd, EPOLL_CTL_ADD, pipefd[0], &ev) == 0) {
            write(pipefd[1], "x", 1);

            /* First event should fire */
            struct epoll_event out;
            int n = epoll_wait(epfd, &out, 1, 100);
            if (n == 1 && (out.events & EPOLLIN)) {
                /* KEY: MOD should succeed to re-arm after ONESHOT fired */
                ev.events = EPOLLIN | EPOLLONESHOT;
                ev.data.fd = pipefd[0];
                if (epoll_ctl(epfd, EPOLL_CTL_MOD, pipefd[0], &ev) == 0) {
                    /* Write again to verify re-arm */
                    write(pipefd[1], "y", 1);
                    n = epoll_wait(epfd, &out, 1, 100);
                    EXPECT_TRUE(n == 1 && (out.events & EPOLLIN),
                                "re-armed EPOLLONESHOT did not fire");
                } else {
                    FAIL(
                        "EPOLL_CTL_MOD failed after ONESHOT (this was the "
                        "bug)");
                }
            } else {
                FAIL("initial EPOLLONESHOT did not fire");
            }
        } else {
            FAIL("epoll_ctl ADD with EPOLLONESHOT failed");
        }

        close(pipefd[0]);
        close(pipefd[1]);
        close(epfd);
    }

    SUMMARY("test-epoll");
    return fails > 0 ? 1 : 0;
}
