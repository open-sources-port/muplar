/* Test clone3 syscall in elfuse
 *
 * Copyright 2026 elfuse contributors
 * Copyright 2025 Moritz Angermann, zw3rk pte. ltd.
 * SPDX-License-Identifier: Apache-2.0
 *
 * Tests clone3 for both fork (new process) and thread (CLONE_THREAD)
 * paths, verifying that the clone_args struct is correctly parsed and
 * delegated to the existing clone infrastructure.
 */

#include <stdarg.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/wait.h>

#include "test-harness.h"
#include "raw-syscall.h"
#include "test-util.h"

int passes = 0, fails = 0;

static void check(int cond, const char *fmt, ...)
{
    if (cond) {
        passes++;
        return;
    }

    printf("FAIL: ");
    va_list ap;
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    printf("\n");
    fails++;
}

#define CHECK(cond, ...) check((cond), __VA_ARGS__)

/* Linux clone3 constants (aarch64 asm-generic) */
#ifndef __NR_clone3
#define __NR_clone3 435
#endif

#define CLONE3_THREAD 0x00010000
#define CLONE3_VM 0x00000100
#define CLONE3_SIGHAND 0x00000800
#define CLONE3_FILES 0x00000400
#define CLONE3_FS 0x00000200
#define CLONE3_CHILD_CLEARTID 0x00200000
#define CLONE3_CHILD_SETTID 0x01000000

/* clone_args struct matching kernel v5.3+ layout */
struct clone_args {
    uint64_t flags, pidfd;
    uint64_t child_tid, parent_tid;
    uint64_t exit_signal, stack, stack_size, tls, set_tid, set_tid_size, cgroup;
};

#define CLONE_ARGS_SIZE_VER0 64 /* first 8 fields */

/* Shared state for thread test */
static volatile int thread_done = 0;
static volatile int thread_result = 0;
static volatile int thread_tid = 0;

/* Thread entry: sets thread_result and signals done via futex */
static int thread_fn(void)
{
    thread_result = 42;
    __atomic_store_n(&thread_done, 1, __ATOMIC_SEQ_CST);
    raw_syscall6(__NR_futex, (long) &thread_done,
                 FUTEX_WAKE | FUTEX_PRIVATE_FLAG, 1, 0, 0, 0);
    raw_exit(0);
    test_unreachable();
}

/* Test 1: clone3 basic fork */
static void test_fork(void)
{
    struct clone_args ca;
    memset(&ca, 0, sizeof(ca));
    ca.exit_signal = 17; /* SIGCHLD on aarch64 */

    long ret = raw_clone3(&ca, CLONE_ARGS_SIZE_VER0);
    if (ret < 0) {
        CHECK(0, "clone3 fork failed with %ld", ret);
        return;
    }

    if (ret == 0) {
        /* Child: exit with distinctive code */
        _exit(7);
    }

    /* Parent: wait for child */
    int status = 0;
    pid_t waited = waitpid((pid_t) ret, &status, 0);
    CHECK(waited == (pid_t) ret, "waitpid returned %d, expected %ld", waited,
          ret);
    CHECK(WIFEXITED(status) && WEXITSTATUS(status) == 7,
          "child exit status: 0x%x (expected exit 7)", status);
}

/* Test 2: clone3 with thread */
static void test_thread(void)
{
    /* Allocate a stack for the child thread */
    size_t stack_size = 65536;
    void *stack = mmap(NULL, stack_size, PROT_READ | PROT_WRITE,
                       MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    CHECK(stack != MAP_FAILED, "mmap for thread stack failed");
    if (stack == MAP_FAILED)
        return;

    thread_done = 0;
    thread_result = 0;
    thread_tid = 0;

    struct clone_args ca;
    memset(&ca, 0, sizeof(ca));
    ca.flags = CLONE3_THREAD | CLONE3_VM | CLONE3_SIGHAND | CLONE3_FILES |
               CLONE3_FS | CLONE3_CHILD_CLEARTID | CLONE3_CHILD_SETTID;
    ca.exit_signal = 0; /* threads do not send signals on exit */
    ca.stack = (uint64_t) stack;
    ca.stack_size = stack_size;
    ca.child_tid = (uint64_t) &thread_tid; /* set_child_tid / clear_child_tid */

    /* Point the child's PC at thread_fn.
     * clone3 with CLONE_THREAD resumes at the parent's PC but with X0=0.
     * The caller needs the child to jump to thread_fn, so the caller sets up
     * the stack with a return address. Actually, clone resumes at parent PC
     * with X0=0 and the new stack, so the caller needs to arrange for the child
     * to call thread_fn from the clone return path.
     *
     * For raw clone, the child returns from the syscall with X0=0.
     * The code checks X0 in the caller. But since this is inline asm returning
     * to the same PC, the test handles it by checking the return value.
     */

    long ret = raw_clone3(&ca, CLONE_ARGS_SIZE_VER0);
    if (ret == 0) {
        /* Child thread: the child is on the new stack */
        thread_fn();
        __builtin_unreachable();
    }

    CHECK(ret > 0, "clone3 thread returned %ld", ret);
    if (ret < 0) {
        munmap(stack, stack_size);
        return;
    }

    /* Wait for child thread via futex on thread_done */
    int attempts = 0;
    while (__atomic_load_n(&thread_done, __ATOMIC_SEQ_CST) == 0 &&
           attempts < 1000) {
        raw_syscall6(__NR_futex, (long) &thread_done,
                     FUTEX_WAIT | FUTEX_PRIVATE_FLAG, 0, 0, 0, 0);
        attempts++;
    }

    CHECK(thread_done == 1, "thread_done=%d (expected 1)", thread_done);
    CHECK(thread_result == 42, "thread_result=%d (expected 42)", thread_result);

    /* Wait for CLONE_CHILD_CLEARTID before unmapping the child's stack. */
    while (__atomic_load_n(&thread_tid, __ATOMIC_SEQ_CST) != 0) {
        int tid = __atomic_load_n(&thread_tid, __ATOMIC_SEQ_CST);
        raw_syscall6(__NR_futex, (long) &thread_tid,
                     FUTEX_WAIT | FUTEX_PRIVATE_FLAG, tid, 0, 0, 0);
    }

    munmap(stack, stack_size);
}

/* Test 3: clone3 rejects invalid size */
static void test_invalid_size(void)
{
    struct clone_args ca;
    memset(&ca, 0, sizeof(ca));
    ca.exit_signal = 17;

    /* Size too small */
    long ret = raw_clone3(&ca, 32);
    CHECK(ret == -22 /* EINVAL */,
          "clone3 size=32 returned %ld (expected -EINVAL)", ret);

    /* Size zero */
    ret = raw_clone3(&ca, 0);
    CHECK(ret == -22, "clone3 size=0 returned %ld (expected -EINVAL)", ret);
}

/* Test 4: clone3 rejects unsupported flags */
static void test_unsupported_flags(void)
{
    struct clone_args ca;
    memset(&ca, 0, sizeof(ca));
    ca.exit_signal = 17;

    /* CLONE_PIDFD: now supported. Clone should succeed (returns child PID
     * in parent, 0 in child).  Reap the child to avoid zombies.
     */
    int32_t pidfd_out = -1;
    ca.flags = 0x00001000; /* CLONE_PIDFD */
    ca.pidfd = (uint64_t) &pidfd_out;
    long ret = raw_clone3(&ca, CLONE_ARGS_SIZE_VER0);
    if (ret == 0) {
        /* child: exit immediately */
        raw_syscall1(__NR_exit, 0);
        __builtin_unreachable();
    }
    CHECK(ret > 0, "clone3 CLONE_PIDFD returned %ld (expected >0)", ret);
    if (ret > 0) {
        /* Reap child */
        raw_syscall4(__NR_wait4, ret, 0, 0, 0);
        if (pidfd_out >= 0)
            raw_syscall1(__NR_close, pidfd_out);
    }
    ca.pidfd = 0;

    /* CLONE_NEWPID not supported */
    ca.flags = 0x20000000; /* CLONE_NEWPID */
    ret = raw_clone3(&ca, CLONE_ARGS_SIZE_VER0);
    CHECK(ret == -22, "clone3 CLONE_NEWPID returned %ld (expected -EINVAL)",
          ret);
}

/* Test 5: clone3 with larger struct (zero tail = ok) */
static void test_larger_struct(void)
{
    /* Allocate a larger buffer, zero-filled */
    char buf[256];
    memset(buf, 0, sizeof(buf));
    struct clone_args *ca = (struct clone_args *) buf;
    ca->exit_signal = 17;

    long ret = raw_clone3(ca, sizeof(buf));
    if (ret < 0) {
        CHECK(0, "clone3 with larger struct failed: %ld", ret);
        return;
    }
    if (ret == 0)
        _exit(0);
    int status = 0;
    waitpid((pid_t) ret, &status, 0);
    CHECK(WIFEXITED(status) && WEXITSTATUS(status) == 0,
          "child from larger struct exited with 0x%x", status);
}

/* Test 6: clone3 with non-zero tail = E2BIG */
static void test_nonzero_tail(void)
{
    char buf[256];
    memset(buf, 0, sizeof(buf));
    struct clone_args *ca = (struct clone_args *) buf;
    ca->exit_signal = 17;
    /* Set a byte in the unknown tail */
    buf[sizeof(struct clone_args) + 1] = 0xff;

    long ret = raw_clone3(ca, sizeof(buf));
    CHECK(ret == -7 /* E2BIG */,
          "clone3 non-zero tail returned %ld (expected -E2BIG)", ret);
}

/* Test 7: CSIGNAL bits in flags must be zero */
static void test_csignal_in_flags(void)
{
    struct clone_args ca;
    memset(&ca, 0, sizeof(ca));
    /* Put SIGCHLD (17) in the flags low byte; clone3 forbids this */
    ca.flags = 17;
    ca.exit_signal = 0;

    long ret = raw_clone3(&ca, CLONE_ARGS_SIZE_VER0);
    CHECK(ret == -22, "clone3 flags=17 returned %ld (expected -EINVAL)", ret);

    /* Both flags low byte AND exit_signal set; should also fail */
    ca.flags = 1;
    ca.exit_signal = 17;
    ret = raw_clone3(&ca, CLONE_ARGS_SIZE_VER0);
    CHECK(ret == -22,
          "clone3 flags=1+exit_signal=17 returned %ld (expected -EINVAL)", ret);
}

/* Test 8: CLONE_THREAD + exit_signal != 0 is invalid */
static void test_thread_with_signal(void)
{
    struct clone_args ca;
    memset(&ca, 0, sizeof(ca));
    ca.flags =
        CLONE3_THREAD | CLONE3_VM | CLONE3_SIGHAND | CLONE3_FILES | CLONE3_FS;
    ca.exit_signal = 17; /* SIGCHLD, invalid with CLONE_THREAD */
    ca.stack = 0x10000;  /* non-zero to satisfy stack pair check */
    ca.stack_size = 0x10000;

    long ret = raw_clone3(&ca, CLONE_ARGS_SIZE_VER0);
    CHECK(ret == -22,
          "clone3 CLONE_THREAD+SIGCHLD returned %ld (expected -EINVAL)", ret);
}

/* Test 9: stack/stack_size mismatch */
static void test_stack_mismatch(void)
{
    struct clone_args ca;
    memset(&ca, 0, sizeof(ca));
    ca.exit_signal = 17;

    /* stack != 0 but stack_size == 0 */
    ca.stack = 0x10000;
    ca.stack_size = 0;
    long ret = raw_clone3(&ca, CLONE_ARGS_SIZE_VER0);
    CHECK(ret == -22, "clone3 stack!=0,size=0 returned %ld (expected -EINVAL)",
          ret);

    /* stack == 0 but stack_size != 0 */
    ca.stack = 0;
    ca.stack_size = 0x10000;
    ret = raw_clone3(&ca, CLONE_ARGS_SIZE_VER0);
    CHECK(ret == -22, "clone3 stack=0,size!=0 returned %ld (expected -EINVAL)",
          ret);
}

/* Test 10: stack + stack_size overflow */
static void test_stack_overflow(void)
{
    struct clone_args ca;
    memset(&ca, 0, sizeof(ca));
    ca.flags =
        CLONE3_THREAD | CLONE3_VM | CLONE3_SIGHAND | CLONE3_FILES | CLONE3_FS;
    ca.exit_signal = 0;
    ca.stack = 0xFFFFFFFFFFFF0000ULL;
    ca.stack_size = 0x20000; /* overflows uint64 */

    long ret = raw_clone3(&ca, CLONE_ARGS_SIZE_VER0);
    CHECK(ret == -22, "clone3 stack overflow returned %ld (expected -EINVAL)",
          ret);
}

int main(void)
{
    printf("test-clone3: starting\n");

    test_fork();
    test_thread();
    test_invalid_size();
    test_unsupported_flags();
    test_larger_struct();
    test_nonzero_tail();
    test_csignal_in_flags();
    test_thread_with_signal();
    test_stack_mismatch();
    test_stack_overflow();

    SUMMARY("test-clone3");
    return fails > 0 ? 1 : 0;
}
