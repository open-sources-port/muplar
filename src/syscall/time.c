/* Time and timer syscall handlers
 *
 * Copyright 2026 elfuse contributors
 * Copyright 2025 Moritz Angermann, zw3rk pte. ltd.
 * SPDX-License-Identifier: Apache-2.0
 *
 * Clock, nanosleep, gettimeofday, and interval timer operations. All
 * functions are called from syscall_dispatch() in syscall/syscall.c.
 */

#include <errno.h>
#include <stdbool.h>
#include <time.h>
#include <sys/time.h>

#include "utils.h"

#include "syscall/abi.h"
#include "syscall/internal.h"
#include "syscall/proc.h" /* proc_exit_group_requested */
#include "syscall/signal.h"
#include "syscall/time.h"

/* Linux TIMER_ABSTIME (not defined on all macOS SDK versions) */
#ifndef TIMER_ABSTIME
#define TIMER_ABSTIME 1
#endif

/* Clock ID translation. */

/* Linux dynamic CPU clock ID encoding (kernel/time/posix-cpu-timers.c):
 *
 *   clockid = ~pid << 3 | type [| CPUCLOCK_PERTHREAD_MASK]
 *
 * Where type is:  CPUCLOCK_PROF=0, CPUCLOCK_VIRT=1, CPUCLOCK_SCHED=2
 * CPUCLOCK_PERTHREAD_MASK = 4 (bit 2): set for per-thread, clear for
 * per-process pid = 0 means "self" (current process/thread).
 *
 * Examples: -2 = per-thread SCHED (self), -6 = per-process SCHED (self)
 * Threaded language runtimes use these (via pthread_getcpuclockid()) to
 * measure per-thread CPU time without sampling the whole process.
 */
#define LINUX_CPUCLOCK_PERTHREAD_MASK 4
/* Linux encodes the dynamic-pid clock as ((~pid) << 3) | type. Decoding is
 * ~clockid >> 3, but >> on a signed negative value is implementation-defined.
 * The complement of a negative value (high bit set) is non-negative, so apply
 * ~ via unsigned arithmetic and only then cast to int for a well-defined
 * right shift.
 */
#define LINUX_CPUCLOCK_PID(clk) ((int) (~(unsigned int) (clk)) >> 3)

#define SLEEP_CHUNK_NS 100000000LL /* 100ms */

_Static_assert(sizeof(struct timespec) == sizeof(linux_timespec_t),
               "host and guest timespec must match on LP64");
_Static_assert(sizeof(struct timeval) == sizeof(linux_timeval_t),
               "host and guest timeval must match on LP64");

static bool linux_timespec_valid(const linux_timespec_t *ts,
                                 bool allow_negative_sec)
{
    if (!allow_negative_sec && ts->tv_sec < 0)
        return false;
    return ts->tv_nsec >= 0 && ts->tv_nsec < NSEC_PER_SEC;
}

static int64_t linux_timespec_to_ns_sat(const linux_timespec_t *ts)
{
    if (ts->tv_sec < 0)
        return 0;

    const int64_t max_sec = INT64_MAX / NSEC_PER_SEC;
    const int64_t max_nsec = INT64_MAX % NSEC_PER_SEC;
    if (ts->tv_sec > max_sec ||
        (ts->tv_sec == max_sec && ts->tv_nsec > max_nsec))
        return INT64_MAX;

    return ts->tv_sec * NSEC_PER_SEC + ts->tv_nsec;
}

static int64_t host_timespec_to_ns_sat(const struct timespec *ts)
{
    linux_timespec_t lts = {
        .tv_sec = ts->tv_sec,
        .tv_nsec = ts->tv_nsec,
    };
    return linux_timespec_to_ns_sat(&lts);
}

static struct timespec ns_to_host_timespec(int64_t ns)
{
    if (ns < 0)
        ns = 0;
    return (struct timespec) {
        .tv_sec = ns / NSEC_PER_SEC,
        .tv_nsec = ns % NSEC_PER_SEC,
    };
}

static int write_remaining_sleep(guest_t *g,
                                 uint64_t rem_gva,
                                 int64_t remaining_ns)
{
    if (!rem_gva)
        return 0;

    if (remaining_ns < 0)
        remaining_ns = 0;

    struct timespec rem = ns_to_host_timespec(remaining_ns);
    return guest_write_small(g, rem_gva, &rem, sizeof(rem));
}

static int64_t interruptible_sleep_ns(guest_t *g,
                                      int64_t remaining_ns,
                                      uint64_t rem_gva,
                                      bool write_rem)
{
    while (remaining_ns > 0) {
        if (proc_exit_group_requested() || signal_pending()) {
            if (write_rem &&
                write_remaining_sleep(g, rem_gva, remaining_ns) < 0)
                return -LINUX_EFAULT;
            return -LINUX_EINTR;
        }

        int64_t sleep_ns =
            (remaining_ns < SLEEP_CHUNK_NS) ? remaining_ns : SLEEP_CHUNK_NS;
        struct timespec req = ns_to_host_timespec(sleep_ns);
        struct timespec rem = {0};

        if (nanosleep(&req, &rem) < 0) {
            int64_t slept_ns = sleep_ns - host_timespec_to_ns_sat(&rem);
            if (slept_ns < 0)
                slept_ns = 0;
            remaining_ns -= slept_ns;
            if (write_rem &&
                write_remaining_sleep(g, rem_gva, remaining_ns) < 0)
                return -LINUX_EFAULT;
            return -LINUX_EINTR;
        }
        remaining_ns -= sleep_ns;
    }

    return 0;
}

/* Translate Linux clock IDs to macOS.
 * Linux: REALTIME=0, MONOTONIC=1, PROCESS_CPUTIME=2, THREAD_CPUTIME=3,
 * MONOTONIC_RAW=4 macOS: REALTIME=0, MONOTONIC_RAW=4, MONOTONIC=6,
 * PROCESS_CPUTIME=12, THREAD_CPUTIME=16
 *
 * Negative clock IDs encode Linux dynamic per-process/per-thread CPU clocks.
 * time emulation translates pid=0 (self) clocks to the macOS equivalents; for
 * other pids time emulation returns -1 (no macOS equivalent).
 */
static int translate_clockid(int linux_clockid)
{
    switch (linux_clockid) {
    case 0:
        return CLOCK_REALTIME;
    case 1:
        return CLOCK_MONOTONIC;
    case 2:
        return CLOCK_PROCESS_CPUTIME_ID;
    case 3:
        return CLOCK_THREAD_CPUTIME_ID;
    case 4:
        return CLOCK_MONOTONIC_RAW;
    case 5:
        return CLOCK_REALTIME; /* CLOCK_REALTIME_COARSE */
    case 6:
        return CLOCK_MONOTONIC; /* CLOCK_MONOTONIC_COARSE */
    case 7:
        /* CLOCK_BOOTTIME includes suspend time on Linux; macOS exposes no
         * suspend-aware equivalent, so use monotonic time.
         */
        return CLOCK_MONOTONIC;
    default:
        /* Handle Linux dynamic CPU clock IDs (negative values).
         * Decode: pid = ~(clockid >> 3), perthread = clockid & 4
         * Only support pid=0 (self); other pids have no macOS equivalent.
         */
        if (linux_clockid < 0) {
            int pid = LINUX_CPUCLOCK_PID(linux_clockid);
            if (pid != 0)
                return -1; /* Other process CPU times are unavailable */
            bool is_perthread = linux_clockid & LINUX_CPUCLOCK_PERTHREAD_MASK;
            return is_perthread ? CLOCK_THREAD_CPUTIME_ID
                                : CLOCK_PROCESS_CPUTIME_ID;
        }
        return -1;
    }
}

/* Linux itimerval type. */

/* Linux struct itimerval (same layout as macOS on LP64) */
typedef struct {
    linux_timeval_t it_interval, it_value;
} linux_itimerval_t;

/* Time/timer syscall handlers. */

int64_t sys_clock_getres(guest_t *g, int clockid, uint64_t tp_gva)
{
    int mac_clockid = translate_clockid(clockid);
    if (mac_clockid < 0)
        return -LINUX_EINVAL;

    /* Linux permits NULL here as a clock-id validation query. */
    if (!tp_gva)
        return 0;

    struct timespec ts;
    if (clock_getres(mac_clockid, &ts) < 0)
        return linux_errno();

    if (guest_write_small(g, tp_gva, &ts, sizeof(ts)) < 0)
        return -LINUX_EFAULT;

    return 0;
}

int64_t sys_clock_gettime(guest_t *g, int clockid, uint64_t tp_gva)
{
    struct timespec ts;
    int mac_clockid = translate_clockid(clockid);
    if (mac_clockid < 0)
        return -LINUX_EINVAL;
    if (clock_gettime(mac_clockid, &ts) < 0)
        return linux_errno();

    if (guest_write_small(g, tp_gva, &ts, sizeof(ts)) < 0)
        return -LINUX_EFAULT;

    return 0;
}

/* Interruptible nanosleep: break long sleeps into 100ms chunks so
 * exit_group and pending signals can be checked periodically. Without
 * this, a thread blocked in a multi-second nanosleep cannot be
 * interrupted by exit_group because hv_vcpus_exit only breaks hv_vcpu_run,
 * not host-side blocking syscalls.
 */
int64_t sys_nanosleep(guest_t *g, uint64_t req_gva, uint64_t rem_gva)
{
    linux_timespec_t lreq;
    if (guest_read_small(g, req_gva, &lreq, sizeof(lreq)) < 0)
        return -LINUX_EFAULT;

    if (!linux_timespec_valid(&lreq, false))
        return -LINUX_EINVAL;

    return interruptible_sleep_ns(g, linux_timespec_to_ns_sat(&lreq), rem_gva,
                                  true);
}

int64_t sys_clock_nanosleep(guest_t *g,
                            int clockid,
                            int flags,
                            uint64_t req_gva,
                            uint64_t rem_gva)
{
    linux_timespec_t lreq;
    if (guest_read_small(g, req_gva, &lreq, sizeof(lreq)) < 0)
        return -LINUX_EFAULT;

    if (flags & ~TIMER_ABSTIME)
        return -LINUX_EINVAL;
    if (!linux_timespec_valid(&lreq, (flags & TIMER_ABSTIME) != 0))
        return -LINUX_EINVAL;

    int mac_clockid = translate_clockid(clockid);
    if (mac_clockid < 0)
        return -LINUX_EINVAL;

    int64_t remaining_ns;

    if (flags & TIMER_ABSTIME) {
        if (lreq.tv_sec < 0)
            return 0;

        struct timespec now;
        if (clock_gettime(mac_clockid, &now) < 0)
            return linux_errno();

        int64_t target_ns = linux_timespec_to_ns_sat(&lreq);
        int64_t now_ns = host_timespec_to_ns_sat(&now);
        int64_t delta_ns = target_ns - now_ns;
        if (delta_ns <= 0)
            return 0; /* Already past */
        remaining_ns = delta_ns;
    } else {
        remaining_ns = linux_timespec_to_ns_sat(&lreq);
    }

    return interruptible_sleep_ns(g, remaining_ns, rem_gva,
                                  (flags & TIMER_ABSTIME) == 0);
}

int64_t sys_gettimeofday(guest_t *g, uint64_t tv_gva, uint64_t tz_gva)
{
    (void) tz_gva; /* timezone is obsolete */
    struct timeval tv;
    if (gettimeofday(&tv, NULL) < 0)
        return linux_errno();

    if (tv_gva && guest_write_small(g, tv_gva, &tv, sizeof(tv)) < 0)
        return -LINUX_EFAULT;
    return 0;
}

int64_t sys_setitimer(guest_t *g, int which, uint64_t new_gva, uint64_t old_gva)
{
    /* Linux reads new_gva before dispatching on which, so an invalid
     * pointer takes precedence over an invalid which value (EFAULT > EINVAL).
     */
    linux_itimerval_t lnew;
    bool has_new = false;
    if (new_gva) {
        if (guest_read_small(g, new_gva, &lnew, sizeof(lnew)) < 0)
            return -LINUX_EFAULT;
        /* Linux rejects tv_usec outside [0, 999999] for value and interval. */
        if (!RANGE_CHECK(lnew.it_value.tv_usec, 0, 1000000) ||
            !RANGE_CHECK(lnew.it_interval.tv_usec, 0, 1000000))
            return -LINUX_EINVAL;
        has_new = true;
    }

    if (which != 0 && which != 1 && which != 2)
        return -LINUX_EINVAL;

    struct timeval val = {0}, itv = {0};
    if (has_new) {
        val = (struct timeval) {.tv_sec = (long) lnew.it_value.tv_sec,
                                .tv_usec = (int) lnew.it_value.tv_usec};
        itv = (struct timeval) {.tv_sec = (long) lnew.it_interval.tv_sec,
                                .tv_usec = (int) lnew.it_interval.tv_usec};
    }

    struct timeval old_val = {0}, old_itv = {0};
    struct timeval *new_val_p = has_new ? &val : NULL;
    struct timeval *new_itv_p = has_new ? &itv : NULL;
    struct timeval *old_val_p = old_gva ? &old_val : NULL;
    struct timeval *old_itv_p = old_gva ? &old_itv : NULL;

    /* ITIMER_REAL is emulated internally because macOS shares alarm() and
     * setitimer(ITIMER_REAL) as the same underlying timer, and elfuse needs
     * alarm() for its per-iteration vCPU timeout. ITIMER_VIRTUAL/PROF must also
     * be emulated since SIGVTALRM/SIGPROF would otherwise hit the host process.
     */
    if (which == 0)
        signal_set_itimer(new_val_p, new_itv_p, old_val_p, old_itv_p);
    else
        signal_set_itimer_virt(which, new_val_p, new_itv_p, old_val_p,
                               old_itv_p);

    if (old_gva) {
        linux_itimerval_t lold = {
            .it_interval = {.tv_sec = old_itv.tv_sec,
                            .tv_usec = old_itv.tv_usec},
            .it_value = {.tv_sec = old_val.tv_sec, .tv_usec = old_val.tv_usec},
        };
        if (guest_write_small(g, old_gva, &lold, sizeof(lold)) < 0)
            return -LINUX_EFAULT;
    }
    return 0;
}

int64_t sys_getitimer(guest_t *g, int which, uint64_t val_gva)
{
    /* ITIMER_REAL/VIRTUAL/PROF are all emulated internally
     * (see sys_setitimer comment).
     */
    struct timeval val, itv;
    if (which == 0)
        signal_get_itimer(&val, &itv);
    else if (which == 1 || which == 2)
        signal_get_itimer_virt(which, &val, &itv);
    else
        return -LINUX_EINVAL;

    linux_itimerval_t lval = {
        .it_interval = {.tv_sec = itv.tv_sec, .tv_usec = itv.tv_usec},
        .it_value = {.tv_sec = val.tv_sec, .tv_usec = val.tv_usec},
    };
    if (guest_write_small(g, val_gva, &lval, sizeof(lval)) < 0)
        return -LINUX_EFAULT;
    return 0;
}
