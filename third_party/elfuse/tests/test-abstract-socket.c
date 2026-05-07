/* Test abstract Unix socket namespace emulation
 *
 * Copyright 2026 elfuse contributors
 * SPDX-License-Identifier: Apache-2.0
 *
 * Tests: bind, connect, accept, getsockname reverse-translation,
 *        EADDRINUSE on double-bind, cleanup on close
 */

#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>

#include "test-harness.h"

/* Build a Linux abstract sockaddr: family(2) + '\0' + name */
static int make_abstract_addr(struct sockaddr_un *addr, const char *name)
{
    memset(addr, 0, sizeof(*addr));
    addr->sun_family = AF_UNIX;
    addr->sun_path[0] = '\0';
    int namelen = (int) strlen(name);
    memcpy(addr->sun_path + 1, name, (size_t) namelen);
    return (int) (sizeof(addr->sun_family) + 1 + namelen);
}

int main(void)
{
    int passes = 0, fails = 0;

    printf("test-abstract-socket: abstract Unix socket namespace\n");

    /* Bind an abstract socket */
    TEST("bind abstract socket");
    int srv = socket(AF_UNIX, SOCK_STREAM, 0);
    if (srv < 0) {
        FAIL("socket() failed");
        goto done;
    }
    {
        struct sockaddr_un addr;
        int addrlen = make_abstract_addr(&addr, "elfuse-test-abstract");
        if (bind(srv, (struct sockaddr *) &addr, (socklen_t) addrlen) == 0)
            PASS();
        else
            FAIL("bind failed");
    }

    /* Listen */
    TEST("listen on abstract socket");
    EXPECT_TRUE(listen(srv, 1) == 0, "listen failed");

    /* getsockname should return abstract address */
    TEST("getsockname returns abstract");
    {
        struct sockaddr_un got;
        socklen_t got_len = sizeof(got);
        memset(&got, 0, sizeof(got));
        if (getsockname(srv, (struct sockaddr *) &got, &got_len) == 0) {
            /* Expect: family=AF_UNIX, sun_path[0]='\0',
             * rest = "elfuse-test-abstract"
             */
            if (got.sun_family == AF_UNIX && got.sun_path[0] == '\0' &&
                !memcmp(got.sun_path + 1, "elfuse-test-abstract", 20))
                PASS();
            else
                FAIL("address mismatch");
        } else {
            FAIL("getsockname failed");
        }
    }

    /* Connect to the abstract socket */
    TEST("connect to abstract socket");
    int cli = socket(AF_UNIX, SOCK_STREAM, 0);
    if (cli < 0) {
        FAIL("client socket() failed");
    } else {
        struct sockaddr_un addr;
        int addrlen = make_abstract_addr(&addr, "elfuse-test-abstract");
        if (connect(cli, (struct sockaddr *) &addr, (socklen_t) addrlen) == 0)
            PASS();
        else
            FAIL("connect failed");
    }

    /* Accept */
    TEST("accept from abstract socket");
    int acc = -1;
    if (srv >= 0) {
        acc = accept(srv, NULL, NULL);
        EXPECT_TRUE(acc >= 0, "accept failed");
    } else {
        FAIL("no server socket");
    }

    /* Data round-trip through accepted connection */
    TEST("data round-trip");
    if (cli >= 0 && acc >= 0) {
        const char *msg = "hello-abstract";
        if (write(cli, msg, strlen(msg)) == (ssize_t) strlen(msg)) {
            char buf[32] = {0};
            ssize_t n = read(acc, buf, sizeof(buf));
            if (n == (ssize_t) strlen(msg) && !memcmp(buf, msg, strlen(msg)))
                PASS();
            else
                FAIL("data mismatch");
        } else {
            FAIL("write failed");
        }
    } else {
        FAIL("no connection");
    }

    /* Double-bind to same abstract name should fail */
    TEST("double-bind returns EADDRINUSE");
    {
        int s2 = socket(AF_UNIX, SOCK_STREAM, 0);
        if (s2 >= 0) {
            struct sockaddr_un addr;
            int addrlen = make_abstract_addr(&addr, "elfuse-test-abstract");
            int rc = bind(s2, (struct sockaddr *) &addr, (socklen_t) addrlen);
            if (rc < 0)
                PASS(); /* Expected failure (EADDRINUSE) */
            else
                FAIL("double-bind succeeded");
            close(s2);
        } else {
            FAIL("socket() failed");
        }
    }

    /* Close server; abstract name should be released */
    if (acc >= 0)
        close(acc);
    if (cli >= 0)
        close(cli);
    if (srv >= 0)
        close(srv);

    /* After close, should be able to re-bind */
    TEST("re-bind after close");
    {
        int s3 = socket(AF_UNIX, SOCK_STREAM, 0);
        if (s3 >= 0) {
            struct sockaddr_un addr;
            int addrlen = make_abstract_addr(&addr, "elfuse-test-abstract");
            if (bind(s3, (struct sockaddr *) &addr, (socklen_t) addrlen) == 0)
                PASS();
            else
                FAIL("re-bind failed");
            close(s3);
        } else {
            FAIL("socket() failed");
        }
    }

done:
    SUMMARY("test-abstract-socket");
    return fails ? 1 : 0;
}
