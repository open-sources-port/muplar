/* Low-base nested fork regression test
 *
 * Copyright 2026 elfuse contributors
 * SPDX-License-Identifier: Apache-2.0
 *
 * Exercises the legacy fork IPC path from a low-linked ET_EXEC. The child
 * forks again, so the grandchild only runs correctly if the intermediate child
 * preserved the executable's true low load address when cloning guest state.
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>

#include "test-harness.h"

int passes = 0, fails = 0;

static void test_binary_is_low_linked(void)
{
    TEST("binary linked below 0x400000");
    uintptr_t pc = (uintptr_t) &test_binary_is_low_linked;
    EXPECT_TRUE(pc < 0x400000ULL, "test binary not linked at low address");
}

static void test_nested_fork_lowbase(void)
{
    TEST("nested fork from low-base ET_EXEC");

    int pipefd[2];
    if (pipe(pipefd) != 0) {
        FAIL("pipe() failed");
        return;
    }

    pid_t child = fork();
    if (child < 0) {
        close(pipefd[0]);
        close(pipefd[1]);
        FAIL("fork() failed");
        return;
    }

    if (child == 0) {
        close(pipefd[0]);

        pid_t grandchild = fork();
        if (grandchild < 0)
            _exit(101);

        if (grandchild == 0) {
            char ok = 'G';
            if (write(pipefd[1], &ok, 1) != 1)
                _exit(102);
            _exit(0);
        }

        int status = 0;
        if (waitpid(grandchild, &status, 0) != grandchild)
            _exit(103);
        if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
            _exit(104);
        _exit(0);
    }

    close(pipefd[1]);

    char ok = 0;
    ssize_t n = read(pipefd[0], &ok, 1);
    close(pipefd[0]);

    int status = 0;
    if (waitpid(child, &status, 0) != child) {
        FAIL("waitpid(child) failed");
        return;
    }

    EXPECT_TRUE(
        n == 1 && ok == 'G' && WIFEXITED(status) && WEXITSTATUS(status) == 0,
        "grandchild did not complete from low-base nested fork");
}

int main(void)
{
    printf("test-fork-lowbase: starting\n");

    test_binary_is_low_linked();
    test_nested_fork_lowbase();

    SUMMARY("test-fork-lowbase");
    return fails ? 1 : 0;
}
