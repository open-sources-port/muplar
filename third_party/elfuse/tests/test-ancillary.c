/* Test ancillary data: SCM_RIGHTS and MSG_CTRUNC
 *
 * Copyright 2026 elfuse contributors
 * SPDX-License-Identifier: Apache-2.0
 *
 * Tests: SCM_RIGHTS fd passing, MSG_CTRUNC on small controllen,
 *        fd translation correctness
 */

#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/socket.h>

#include "test-harness.h"

/* Linux cmsg layout:
 *   cmsg_len:  uint64_t (8 bytes)
 *   cmsg_level: int32_t (4 bytes)
 *   cmsg_type:  int32_t (4 bytes)
 *   data:      at offset 16
 * CMSG_SPACE on Linux aarch64 = ALIGN8(16 + datalen)
 */

/* Send a single fd over a socket via SCM_RIGHTS with one-byte payload */
static ssize_t send_fd(int sock, int fd, char payload)
{
    struct iovec iov = {.iov_base = &payload, .iov_len = 1};
    union {
        char ctrl[CMSG_SPACE(sizeof(int))];
        struct cmsghdr align;
    } cmsg_buf;
    memset(&cmsg_buf, 0, sizeof(cmsg_buf));

    struct msghdr msg = {
        .msg_iov = &iov,
        .msg_iovlen = 1,
        .msg_control = cmsg_buf.ctrl,
        .msg_controllen = sizeof(cmsg_buf.ctrl),
    };
    struct cmsghdr *cmsg = CMSG_FIRSTHDR(&msg);
    cmsg->cmsg_level = SOL_SOCKET;
    cmsg->cmsg_type = SCM_RIGHTS;
    cmsg->cmsg_len = CMSG_LEN(sizeof(int));
    memcpy(CMSG_DATA(cmsg), &fd, sizeof(int));

    return sendmsg(sock, &msg, 0);
}

int main(void)
{
    int passes = 0, fails = 0;
    int sv[2];

    printf("test-ancillary: SCM_RIGHTS and MSG_CTRUNC tests\n");

    /* Create socketpair */
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) < 0) {
        FAIL("socketpair");
        goto done;
    }

    /* Test 1: Send and receive a file descriptor via SCM_RIGHTS */
    TEST("SCM_RIGHTS send/recv fd");
    {
        /* Open a pipe to get a known fd */
        int pipefd[2];
        if (pipe(pipefd) < 0) {
            FAIL("pipe");
            goto done;
        }

        /* Write something into the pipe so we can verify the received fd */
        const char *probe = "ancillary-probe";
        write(pipefd[1], probe, strlen(probe));
        close(pipefd[1]);

        /* Send pipefd[0] via SCM_RIGHTS */
        if (send_fd(sv[0], pipefd[0], 'x') < 0) {
            FAIL("sendmsg");
            close(pipefd[0]);
            goto done;
        }
        close(pipefd[0]);

        /* Receive */
        char rbuf;
        struct iovec riov = {.iov_base = &rbuf, .iov_len = 1};
        union {
            char ctrl[CMSG_SPACE(sizeof(int))];
            struct cmsghdr align;
        } rcmsg_buf;
        memset(&rcmsg_buf, 0, sizeof(rcmsg_buf));

        struct msghdr rmsg = {
            .msg_iov = &riov,
            .msg_iovlen = 1,
            .msg_control = rcmsg_buf.ctrl,
            .msg_controllen = sizeof(rcmsg_buf.ctrl),
        };

        if (recvmsg(sv[1], &rmsg, 0) < 0) {
            FAIL("recvmsg");
            goto done;
        }

        /* Extract received fd */
        struct cmsghdr *rcmsg = CMSG_FIRSTHDR(&rmsg);
        if (!rcmsg || rcmsg->cmsg_level != SOL_SOCKET ||
            rcmsg->cmsg_type != SCM_RIGHTS) {
            FAIL("no SCM_RIGHTS in received msg");
            goto done;
        }

        int recv_fd;
        memcpy(&recv_fd, CMSG_DATA(rcmsg), sizeof(int));

        /* Verify the received fd is usable: read the probe data */
        char verify[32] = {0};
        ssize_t n = read(recv_fd, verify, sizeof(verify));
        close(recv_fd);

        if (n == (ssize_t) strlen(probe) &&
            !memcmp(verify, probe, strlen(probe)))
            PASS();
        else
            FAIL("received fd data mismatch");
    }

    /* Test 2: MSG_CTRUNC when controllen is too small */
    TEST("MSG_CTRUNC on small controllen");
    {
        /* Send an fd */
        int devnull = open("/dev/null", O_RDONLY);
        if (devnull < 0) {
            FAIL("open /dev/null");
            goto done;
        }

        if (send_fd(sv[0], devnull, 'y') < 0) {
            FAIL("sendmsg");
            close(devnull);
            goto done;
        }
        close(devnull);

        /* Receive with intentionally small controllen */
        char rbuf;
        struct iovec riov = {.iov_base = &rbuf, .iov_len = 1};
        char tiny_ctrl[4]; /* Way too small for SCM_RIGHTS */
        memset(tiny_ctrl, 0, sizeof(tiny_ctrl));

        struct msghdr rmsg = {
            .msg_iov = &riov,
            .msg_iovlen = 1,
            .msg_control = tiny_ctrl,
            .msg_controllen = sizeof(tiny_ctrl),
        };

        ssize_t ret = recvmsg(sv[1], &rmsg, 0);
        if (ret < 0) {
            FAIL("recvmsg failed");
            goto done;
        }

        /* Check MSG_CTRUNC flag is set */
        EXPECT_TRUE(rmsg.msg_flags & MSG_CTRUNC, "MSG_CTRUNC not set");
    }

    /* Test 3: No ancillary data; controllen should be 0 */
    TEST("no ancillary: controllen=0");
    {
        char buf = 'z';
        write(sv[0], &buf, 1);

        char rbuf;
        struct iovec riov = {.iov_base = &rbuf, .iov_len = 1};
        union {
            char ctrl[CMSG_SPACE(sizeof(int))];
            struct cmsghdr align;
        } rcmsg_buf;
        memset(&rcmsg_buf, 0, sizeof(rcmsg_buf));

        struct msghdr rmsg = {
            .msg_iov = &riov,
            .msg_iovlen = 1,
            .msg_control = rcmsg_buf.ctrl,
            .msg_controllen = sizeof(rcmsg_buf.ctrl),
        };

        if (recvmsg(sv[1], &rmsg, 0) > 0) {
            EXPECT_TRUE(rmsg.msg_controllen == 0, "controllen not 0");
        } else {
            FAIL("recvmsg failed");
        }
    }

done:
    close(sv[0]);
    close(sv[1]);

    SUMMARY("test-ancillary");
    return fails ? 1 : 0;
}
