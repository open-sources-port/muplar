/* COW fork memory isolation tests
 *
 * Copyright 2026 elfuse contributors
 * Copyright 2025 Moritz Angermann, zw3rk pte. ltd.
 * SPDX-License-Identifier: Apache-2.0
 *
 * Verifies that parent and child processes have independent memory
 * after fork (copy-on-write semantics). Parent writes should not
 * appear in child, and vice versa.
 *
 * Syscalls exercised: clone/fork, waitpid, pipe, read, write, mmap, brk
 */

#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/mman.h>
#include <sys/syscall.h>

#include "test-harness.h"

int passes = 0, fails = 0;

/* Test 1: Stack variable isolation */

static void test_stack_isolation(void)
{
    TEST("fork: stack isolation");

    int pipefd[2];
    if (pipe(pipefd) != 0) {
        FAIL("pipe");
        return;
    }

    volatile int x = 42;
    pid_t pid = fork();
    if (pid < 0) {
        FAIL("fork");
        return;
    }

    if (pid == 0) {
        /* Child: modify x, send its value back */
        close(pipefd[0]);
        x = 99;
        int val = (int) x;
        write(pipefd[1], &val, sizeof(val));
        close(pipefd[1]);
        _exit(0);
    }

    /* Parent: modify x differently, read child's value */
    close(pipefd[1]);
    x = 77;

    int child_val = 0;
    read(pipefd[0], &child_val, sizeof(child_val));
    close(pipefd[0]);

    int status;
    waitpid(pid, &status, 0);

    /* Child should see 99, parent sees 77 (independent copies) */
    EXPECT_TRUE(child_val == 99 && x == 77, "stack not isolated");
}

/* Test 2: Heap variable isolation */

static void test_heap_isolation(void)
{
    TEST("fork: heap isolation");

    int pipefd[2];
    if (pipe(pipefd) != 0) {
        FAIL("pipe");
        return;
    }

    int *heap = malloc(sizeof(int));
    if (!heap) {
        FAIL("malloc");
        return;
    }
    *heap = 100;

    pid_t pid = fork();
    if (pid < 0) {
        FAIL("fork");
        free(heap);
        return;
    }

    if (pid == 0) {
        close(pipefd[0]);
        *heap = 200;
        write(pipefd[1], heap, sizeof(int));
        close(pipefd[1]);
        _exit(0);
    }

    close(pipefd[1]);
    *heap = 300;

    int child_val = 0;
    read(pipefd[0], &child_val, sizeof(child_val));
    close(pipefd[0]);

    int status;
    waitpid(pid, &status, 0);

    EXPECT_TRUE(child_val == 200 && *heap == 300, "heap not isolated");
    free(heap);
}

/* Test 3: mmap region isolation */

static void test_mmap_isolation(void)
{
    TEST("fork: mmap isolation");

    int pipefd[2];
    if (pipe(pipefd) != 0) {
        FAIL("pipe");
        return;
    }

    void *region = mmap(NULL, 4096, PROT_READ | PROT_WRITE,
                        MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (region == MAP_FAILED) {
        FAIL("mmap");
        return;
    }

    int *val = (int *) region;
    *val = 1000;

    pid_t pid = fork();
    if (pid < 0) {
        FAIL("fork");
        munmap(region, 4096);
        return;
    }

    if (pid == 0) {
        close(pipefd[0]);
        *val = 2000;
        write(pipefd[1], val, sizeof(int));
        close(pipefd[1]);
        _exit(0);
    }

    close(pipefd[1]);
    *val = 3000;

    int child_val = 0;
    read(pipefd[0], &child_val, sizeof(child_val));
    close(pipefd[0]);

    int status;
    waitpid(pid, &status, 0);

    EXPECT_TRUE(child_val == 2000 && *val == 3000, "mmap not isolated");
    munmap(region, 4096);
}

/* Test 4: Large region COW (verify no corruption) */

static void test_large_cow(void)
{
    TEST("fork: 1MiB COW integrity");

    int pipefd[2];
    if (pipe(pipefd) != 0) {
        FAIL("pipe");
        return;
    }

    size_t sz = 1024 * 1024;
    char *buf = mmap(NULL, sz, PROT_READ | PROT_WRITE,
                     MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (buf == MAP_FAILED) {
        FAIL("mmap 1MiB");
        return;
    }

    memset(buf, 'A', sz);

    pid_t pid = fork();
    if (pid < 0) {
        FAIL("fork");
        munmap(buf, sz);
        return;
    }

    if (pid == 0) {
        close(pipefd[0]);
        memset(buf, 'B', sz);

        bool ok = true;
        for (size_t i = 0; i < sz; i += 4096) {
            if (buf[i] != 'B') {
                ok = false;
                break;
            }
        }
        write(pipefd[1], &ok, sizeof(ok));
        close(pipefd[1]);
        _exit(0);
    }

    close(pipefd[1]);

    bool parent_ok = true;
    for (size_t i = 0; i < sz; i += 4096) {
        if (buf[i] != 'A') {
            parent_ok = false;
            break;
        }
    }

    bool child_ok = false;
    if (read(pipefd[0], &child_ok, sizeof(child_ok)) != sizeof(child_ok))
        child_ok = false;
    close(pipefd[0]);

    int status;
    waitpid(pid, &status, 0);

    EXPECT_TRUE(parent_ok && child_ok, "1MiB COW integrity failed");
    munmap(buf, sz);
}

/* Test 5: brk isolation */

/* Use brk syscall directly because musl's sbrk() rejects non-zero increments
 * by design (returns -ENOMEM), so the test must use the raw syscall.
 */
static inline void *raw_brk(void *addr)
{
    return (void *) syscall(__NR_brk, addr);
}

static void test_brk_isolation(void)
{
    TEST("fork: brk isolation");

    int pipefd[2];
    if (pipe(pipefd) != 0) {
        FAIL("pipe");
        return;
    }

    /* Query current brk, then grow by one page */
    void *cur = raw_brk(0);
    void *grown = raw_brk((char *) cur + 4096);
    if (grown != (char *) cur + 4096) {
        FAIL("brk grow parent");
        return;
    }

    int *marker = (int *) cur;
    *marker = 0xCAFE;

    pid_t pid = fork();
    if (pid < 0) {
        FAIL("fork");
        return;
    }

    if (pid == 0) {
        close(pipefd[0]);
        *marker = 0xBEEF;
        void *child_cur = raw_brk(0);
        void *child_grow = raw_brk((char *) child_cur + 4096);
        int grew = (child_grow == (char *) child_cur + 4096) ? 1 : 0;
        write(pipefd[1], &grew, sizeof(grew));
        int val = *marker;
        write(pipefd[1], &val, sizeof(val));
        close(pipefd[1]);
        _exit(0);
    }

    close(pipefd[1]);

    int child_grew = 0, child_marker = 0;
    read(pipefd[0], &child_grew, sizeof(child_grew));
    read(pipefd[0], &child_marker, sizeof(child_marker));
    close(pipefd[0]);

    int status;
    waitpid(pid, &status, 0);

    /* Parent marker unchanged, child grew independently */
    EXPECT_TRUE(*marker == 0xCAFE && child_marker == (int) 0xBEEF && child_grew,
                "brk not isolated");
}

/* Main */

int main(void)
{
    printf("test-cow-fork: COW fork memory isolation tests\n");

    test_stack_isolation();
    test_heap_isolation();
    test_mmap_isolation();
    test_large_cow();
    test_brk_isolation();

    SUMMARY("test-cow-fork");
    return fails > 0 ? 1 : 0;
}
