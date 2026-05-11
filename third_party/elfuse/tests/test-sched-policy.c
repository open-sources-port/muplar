/* Test scheduler policy stub syscalls
 *
 * Copyright 2026 elfuse contributors
 * SPDX-License-Identifier: Apache-2.0
 *
 * Exercises sched_setparam (118), sched_setscheduler (119),
 * sched_getscheduler (120), sched_getparam (121),
 * sched_get_priority_min (125), sched_get_priority_max (126),
 * sched_rr_get_interval (127). The implementation is a stub: getters
 * report SCHED_OTHER and a zero priority, RT priority elevation is
 * refused with -EPERM, SCHED_DEADLINE through the legacy entry point
 * is -EINVAL, and all entries validate guest pointers (-EFAULT).
 */

#include <pthread.h>
#include <sched.h>
#include <stdint.h>
#include <unistd.h>

#include "test-harness.h"
#include "raw-syscall.h"

#define __NR_sched_setparam 118
#define __NR_sched_setscheduler 119
#define __NR_sched_getscheduler 120
#define __NR_sched_getparam 121
#define __NR_sched_get_priority_max 125
#define __NR_sched_get_priority_min 126
#define __NR_sched_rr_get_interval 127

#define LINUX_SCHED_DEADLINE 6
#define LINUX_SCHED_RESET_ON_FORK 0x40000000

struct sched_param_compat {
    int sched_priority;
};

struct ts_compat {
    long tv_sec;
    long tv_nsec;
};

struct worker_result {
    long getscheduler_rc;
    long getparam_rc;
    int param_priority;
    long setscheduler_self_rc;
    long getaffinity_rc;
};

static void *worker_main(void *arg)
{
    struct worker_result *out = arg;
    long tid = raw_syscall0(178 /* gettid */);

    out->getscheduler_rc = raw_syscall1(__NR_sched_getscheduler, tid);

    struct sched_param_compat p = {.sched_priority = 0xdead};
    out->getparam_rc = raw_syscall2(__NR_sched_getparam, tid, (long) &p);
    out->param_priority = p.sched_priority;

    struct sched_param_compat zero = {.sched_priority = 0};
    out->setscheduler_self_rc =
        raw_syscall3(__NR_sched_setscheduler, tid, SCHED_OTHER, (long) &zero);

    unsigned long mask = 0;
    out->getaffinity_rc = raw_syscall3(123 /* sched_getaffinity */, tid,
                                       sizeof(mask), (long) &mask);

    return NULL;
}

int main(void)
{
    int passes = 0, fails = 0;

    printf("test-sched-policy: scheduler policy stub coverage\n");

    TEST("getscheduler(0) == SCHED_OTHER");
    EXPECT_EQ(raw_syscall1(__NR_sched_getscheduler, 0), SCHED_OTHER,
              "default policy is not SCHED_OTHER");

    TEST("getscheduler(-1) == -EINVAL");
    EXPECT_RAW_ERRNO(raw_syscall1(__NR_sched_getscheduler, -1), -EINVAL,
                     "negative pid did not return EINVAL");

    TEST("getscheduler(99999) == -ESRCH");
    EXPECT_RAW_ERRNO(raw_syscall1(__NR_sched_getscheduler, 99999), -ESRCH,
                     "unknown pid did not return ESRCH");

    TEST("getparam(0, &p) zeroes priority");
    {
        struct sched_param_compat p = {.sched_priority = 0x5a5a};
        long rc = raw_syscall2(__NR_sched_getparam, 0, (long) &p);
        EXPECT_TRUE(rc == 0 && p.sched_priority == 0,
                    "getparam did not return zero priority");
    }

    TEST("getparam(0, NULL) == -EINVAL");
    EXPECT_RAW_ERRNO(raw_syscall2(__NR_sched_getparam, 0, 0), -EINVAL,
                     "NULL param pointer did not return EINVAL");

    TEST("getparam(0, bogus) == -EFAULT");
    EXPECT_RAW_ERRNO(raw_syscall2(__NR_sched_getparam, 0, (long) 0x1), -EFAULT,
                     "unmapped param did not return EFAULT");

    TEST("setscheduler(SCHED_OTHER, prio=0) == 0");
    {
        struct sched_param_compat p = {.sched_priority = 0};
        EXPECT_EQ(
            raw_syscall3(__NR_sched_setscheduler, 0, SCHED_OTHER, (long) &p), 0,
            "SCHED_OTHER prio=0 was not accepted");
    }

    TEST("setscheduler(SCHED_OTHER, prio=1) == -EINVAL");
    {
        struct sched_param_compat p = {.sched_priority = 1};
        EXPECT_RAW_ERRNO(
            raw_syscall3(__NR_sched_setscheduler, 0, SCHED_OTHER, (long) &p),
            -EINVAL, "non-zero priority for SCHED_OTHER was accepted");
    }

    TEST("setscheduler(SCHED_BATCH, prio=0) == -EPERM");
    {
        struct sched_param_compat p = {.sched_priority = 0};
        EXPECT_RAW_ERRNO(
            raw_syscall3(__NR_sched_setscheduler, 0, SCHED_BATCH, (long) &p),
            -EPERM, "SCHED_BATCH was accepted without state tracking");
    }

    TEST("setscheduler(SCHED_IDLE, prio=0) == -EPERM");
    {
        struct sched_param_compat p = {.sched_priority = 0};
        EXPECT_RAW_ERRNO(
            raw_syscall3(__NR_sched_setscheduler, 0, SCHED_IDLE, (long) &p),
            -EPERM, "SCHED_IDLE was accepted without state tracking");
    }

    TEST("setscheduler(SCHED_FIFO, prio=50) == -EPERM");
    {
        struct sched_param_compat p = {.sched_priority = 50};
        EXPECT_RAW_ERRNO(
            raw_syscall3(__NR_sched_setscheduler, 0, SCHED_FIFO, (long) &p),
            -EPERM, "SCHED_FIFO with valid prio did not return EPERM");
    }

    TEST("setscheduler(SCHED_FIFO, prio=0) == -EINVAL");
    {
        struct sched_param_compat p = {.sched_priority = 0};
        EXPECT_RAW_ERRNO(
            raw_syscall3(__NR_sched_setscheduler, 0, SCHED_FIFO, (long) &p),
            -EINVAL, "SCHED_FIFO with prio=0 did not return EINVAL");
    }

    TEST("setscheduler(SCHED_FIFO, prio=100) == -EINVAL");
    {
        struct sched_param_compat p = {.sched_priority = 100};
        EXPECT_RAW_ERRNO(
            raw_syscall3(__NR_sched_setscheduler, 0, SCHED_FIFO, (long) &p),
            -EINVAL, "SCHED_FIFO with prio=100 did not return EINVAL");
    }

    TEST("setscheduler(SCHED_RR, prio=50) == -EPERM");
    {
        struct sched_param_compat p = {.sched_priority = 50};
        EXPECT_RAW_ERRNO(
            raw_syscall3(__NR_sched_setscheduler, 0, SCHED_RR, (long) &p),
            -EPERM, "SCHED_RR with valid prio did not return EPERM");
    }

    TEST("setscheduler(SCHED_DEADLINE) == -EINVAL");
    {
        struct sched_param_compat p = {.sched_priority = 0};
        EXPECT_RAW_ERRNO(raw_syscall3(__NR_sched_setscheduler, 0,
                                      LINUX_SCHED_DEADLINE, (long) &p),
                         -EINVAL,
                         "SCHED_DEADLINE legacy syscall did not return EINVAL");
    }

    TEST("setscheduler(SCHED_OTHER|RESET_ON_FORK, prio=0) == 0");
    {
        struct sched_param_compat p = {.sched_priority = 0};
        EXPECT_EQ(
            raw_syscall3(__NR_sched_setscheduler, 0,
                         SCHED_OTHER | LINUX_SCHED_RESET_ON_FORK, (long) &p),
            0, "SCHED_RESET_ON_FORK flag was not accepted");
    }

    TEST("setscheduler(SCHED_FIFO|RESET_ON_FORK, prio=50) == -EPERM");
    {
        struct sched_param_compat p = {.sched_priority = 50};
        EXPECT_RAW_ERRNO(
            raw_syscall3(__NR_sched_setscheduler, 0,
                         SCHED_FIFO | LINUX_SCHED_RESET_ON_FORK, (long) &p),
            -EPERM, "SCHED_RESET_ON_FORK changed SCHED_FIFO semantics");
    }

    TEST("setscheduler(_, _, bogus) + bad pid -> EFAULT");
    EXPECT_RAW_ERRNO(
        raw_syscall3(__NR_sched_setscheduler, 99999, SCHED_OTHER, (long) 0x1),
        -EFAULT, "bad param pointer did not take precedence over ESRCH");

    TEST("setscheduler(garbage policy) == -EINVAL");
    {
        struct sched_param_compat p = {.sched_priority = 0};
        EXPECT_RAW_ERRNO(
            raw_syscall3(__NR_sched_setscheduler, 0, 99, (long) &p), -EINVAL,
            "unknown policy did not return EINVAL");
    }

    TEST("setscheduler(_, _, NULL) == -EINVAL");
    EXPECT_RAW_ERRNO(raw_syscall3(__NR_sched_setscheduler, 0, SCHED_OTHER, 0),
                     -EINVAL, "NULL param pointer did not return EINVAL");

    TEST("setparam(0, prio=0) == 0");
    {
        struct sched_param_compat p = {.sched_priority = 0};
        EXPECT_EQ(raw_syscall2(__NR_sched_setparam, 0, (long) &p), 0,
                  "setparam(prio=0) was rejected");
    }

    TEST("setparam(0, prio=1) == -EINVAL");
    {
        struct sched_param_compat p = {.sched_priority = 1};
        EXPECT_RAW_ERRNO(raw_syscall2(__NR_sched_setparam, 0, (long) &p),
                         -EINVAL, "setparam(prio=1) was accepted");
    }

    TEST("get_priority_min(SCHED_OTHER) == 0");
    EXPECT_EQ(raw_syscall1(__NR_sched_get_priority_min, SCHED_OTHER), 0,
              "min for SCHED_OTHER not 0");

    TEST("get_priority_max(SCHED_OTHER) == 0");
    EXPECT_EQ(raw_syscall1(__NR_sched_get_priority_max, SCHED_OTHER), 0,
              "max for SCHED_OTHER not 0");

    TEST("get_priority_min(SCHED_FIFO) == 1");
    EXPECT_EQ(raw_syscall1(__NR_sched_get_priority_min, SCHED_FIFO), 1,
              "min for SCHED_FIFO not 1");

    TEST("get_priority_max(SCHED_FIFO) == 99");
    EXPECT_EQ(raw_syscall1(__NR_sched_get_priority_max, SCHED_FIFO), 99,
              "max for SCHED_FIFO not 99");

    TEST("get_priority_min(SCHED_DEADLINE) == 0");
    EXPECT_EQ(raw_syscall1(__NR_sched_get_priority_min, LINUX_SCHED_DEADLINE),
              0, "min for SCHED_DEADLINE not 0");

    TEST("get_priority_max(SCHED_DEADLINE) == 0");
    EXPECT_EQ(raw_syscall1(__NR_sched_get_priority_max, LINUX_SCHED_DEADLINE),
              0, "max for SCHED_DEADLINE not 0");

    TEST("get_priority_min(garbage) == -EINVAL");
    EXPECT_RAW_ERRNO(raw_syscall1(__NR_sched_get_priority_min, 42), -EINVAL,
                     "unknown policy min did not return EINVAL");

    TEST("get_priority_max(garbage) == -EINVAL");
    EXPECT_RAW_ERRNO(raw_syscall1(__NR_sched_get_priority_max, 42), -EINVAL,
                     "unknown policy max did not return EINVAL");

    TEST("rr_get_interval(0, &ts) writes plausible slice");
    {
        struct ts_compat ts = {.tv_sec = 7, .tv_nsec = 11};
        long rc = raw_syscall2(__NR_sched_rr_get_interval, 0, (long) &ts);
        EXPECT_TRUE(rc == 0 && ts.tv_sec == 0 && ts.tv_nsec > 0 &&
                        ts.tv_nsec < 1000 * 1000 * 1000L,
                    "rr_get_interval did not write a sane sub-second slice");
    }

    TEST("rr_get_interval(0, NULL) == -EFAULT");
    EXPECT_RAW_ERRNO(raw_syscall2(__NR_sched_rr_get_interval, 0, 0), -EFAULT,
                     "NULL ts pointer did not return EFAULT");

    /* Worker-thread coverage: scheduler syscalls take a TID, not a TGID, so a
     * worker calling sched_*(gettid()) must succeed. This also exercises
     * sched_getaffinity's per-thread pid gate.
     */
    {
        pthread_t worker;
        struct worker_result r = {0};
        if (pthread_create(&worker, NULL, worker_main, &r) != 0) {
            TEST("pthread_create(worker)");
            FAIL("pthread_create failed");
        } else {
            pthread_join(worker, NULL);

            TEST("worker getscheduler(gettid()) == SCHED_OTHER");
            EXPECT_EQ(r.getscheduler_rc, SCHED_OTHER,
                      "worker did not see SCHED_OTHER");

            TEST("worker getparam(gettid()) zeroes priority");
            EXPECT_TRUE(r.getparam_rc == 0 && r.param_priority == 0,
                        "worker getparam mismatch");

            TEST("worker setscheduler(gettid(), OTHER, 0) == 0");
            EXPECT_EQ(r.setscheduler_self_rc, 0,
                      "worker self-setscheduler rejected");

            TEST("worker getaffinity(gettid()) succeeds");
            EXPECT_TRUE(r.getaffinity_rc > 0, "worker getaffinity failed");
        }
    }

    SUMMARY("test-sched-policy");
    return fails == 0 ? 0 : 1;
}
