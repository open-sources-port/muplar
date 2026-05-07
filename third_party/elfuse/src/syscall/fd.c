/* Special FD types (eventfd, signalfd, timerfd)
 *
 * Copyright 2026 elfuse contributors
 * Copyright 2025 Moritz Angermann, zw3rk pte. ltd.
 * SPDX-License-Identifier: Apache-2.0
 *
 * Emulates Linux eventfd (pipe+counter), signalfd (synthetic signal reads),
 * and timerfd (kqueue EVFILT_TIMER). Each provides special read/write/close
 * semantics dispatched from sys_read/sys_write/sys_close.
 */

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <stddef.h>
#include <time.h>
#include <pthread.h>

#include "utils.h"

#include "debug/log.h"
#include <sys/event.h>

#include "syscall/abi.h"
#include "syscall/fd.h"
#include "syscall/internal.h"
#include "syscall/proc.h"
#include "syscall/signal.h"

/* Mutex protecting timerfd/eventfd/signalfd per-slot state. Prevents races when
 * two threads operate on the same special FD concurrently (e.g., two threads
 * reading the same eventfd). Lock order: 5a (after thread_lock(5), never held
 * together with it).
 */
static pthread_mutex_t sfd_lock = PTHREAD_MUTEX_INITIALIZER;

static void timerfd_close(int guest_fd);
static void eventfd_close(int guest_fd);
static void signalfd_close(int guest_fd);

#define NS_PER_SEC 1000000000LL
#define US_PER_SEC 1000000LL

/* All special-FD state arrays store guest_fd as their first field. Keep the
 * common slot walk in one place so timerfd/eventfd/signalfd stay consistent.
 */
static int sfd_find_slot(const void *state,
                         size_t count,
                         size_t stride,
                         int guest_fd)
{
    const uint8_t *base = state;
    for (size_t i = 0; i < count; i++) {
        int slot_guest_fd;
        memcpy(&slot_guest_fd, base + i * stride, sizeof(slot_guest_fd));
        if (slot_guest_fd == guest_fd)
            return (int) i;
    }
    return -1;
}

static int sfd_alloc_slot(const void *state, size_t count, size_t stride)
{
    return sfd_find_slot(state, count, stride, -1);
}

/* timerfd emulation via kqueue EVFILT_TIMER
 *
 * Each timerfd_create() creates a kqueue + timer registration.
 * Reads from the timerfd return an 8-byte counter of expirations.
 */

/* Linux itimerspec for timerfd_settime/gettime */
typedef struct {
    int64_t it_interval_sec, it_interval_nsec, it_value_sec, it_value_nsec;
} linux_itimerspec_t;

/* Per-timerfd state (stored alongside the FD_TIMERFD entry) */
#define TIMERFD_MAX 32
#define LINUX_TFD_NONBLOCK LINUX_O_NONBLOCK
#define LINUX_TFD_CLOEXEC LINUX_O_CLOEXEC
#define LINUX_TFD_TIMER_ABSTIME 1
#define LINUX_TFD_TIMER_CANCEL_ON_SET 2
#define LINUX_CLOCK_REALTIME 0
#define LINUX_CLOCK_MONOTONIC 1

static struct {
    int guest_fd;         /* Guest fd (-1 if unused) */
    int kq_fd;            /* kqueue fd for this timer */
    uint64_t expirations; /* Accumulated expiration count */
    int64_t interval_ns;  /* Repeat interval (0 = one-shot) */
    int64_t initial_ns;   /* Initial value at arm time (for gettime) */
    int64_t arm_time_ns;  /* CLOCK_MONOTONIC time when timer was armed */
    int clockid; /* Linux clock ID (CLOCK_REALTIME=0, CLOCK_MONOTONIC=1) */
    bool armed;  /* True if timer is running */
} timerfd_state[TIMERFD_MAX];

void timerfd_init(void)
{
    for (int i = 0; i < TIMERFD_MAX; i++)
        timerfd_state[i].guest_fd = -1;
}

static int timerfd_find(int guest_fd)
{
    return sfd_find_slot(timerfd_state, TIMERFD_MAX, sizeof(timerfd_state[0]),
                         guest_fd);
}

static int timerfd_alloc(void)
{
    return sfd_alloc_slot(timerfd_state, TIMERFD_MAX, sizeof(timerfd_state[0]));
}

/* Called with sfd_lock held. Drain any kevent expirations sitting on the
 * timer's kqueue and fold them into the slot's accumulator. Used by
 * timerfd_read before consuming the counter and by timerfd_fdinfo_snapshot
 * before reporting it; without this drain, fdinfo would lag the actual
 * fire count by however many ticks were pending in the kqueue.
 */
static void timerfd_drain_pending_locked(int slot)
{
    int kq = timerfd_state[slot].kq_fd;
    struct kevent kev;
    struct timespec ts_zero = {0, 0};
    int nev = kevent(kq, NULL, 0, &kev, 1, &ts_zero);
    if (nev > 0) {
        uint64_t fires = (uint64_t) kev.data;
        if (fires == 0)
            fires = 1; /* At least one expiration */
        timerfd_state[slot].expirations += fires;
    }
}

/* Called with sfd_lock held. Returns nanoseconds until the next expiration,
 * or 0 when the timer is disarmed or a one-shot timer has already expired.
 */
static int64_t timerfd_remaining_ns_locked(int slot, int64_t now_ns)
{
    if (!timerfd_state[slot].armed)
        return 0;

    int64_t elapsed = now_ns - timerfd_state[slot].arm_time_ns;
    if (elapsed < 0)
        elapsed = 0;

    if (timerfd_state[slot].interval_ns > 0) {
        int64_t total = timerfd_state[slot].initial_ns;
        if (elapsed >= total) {
            int64_t since_first = elapsed - total;
            int64_t interval = timerfd_state[slot].interval_ns;
            int64_t remaining = interval - (since_first % interval);
            return remaining == 0 ? interval : remaining;
        }
        return total - elapsed;
    }

    int64_t remaining = timerfd_state[slot].initial_ns - elapsed;
    return remaining > 0 ? remaining : 0;
}

int64_t sys_timerfd_create(int clockid, int flags)
{
    if (clockid != LINUX_CLOCK_REALTIME && clockid != LINUX_CLOCK_MONOTONIC)
        return -LINUX_EINVAL;
    if (flags & ~(LINUX_TFD_CLOEXEC | LINUX_TFD_NONBLOCK))
        return -LINUX_EINVAL;

    int kq = kqueue();
    if (kq < 0)
        return linux_errno();

    if (((flags & LINUX_TFD_CLOEXEC) && fd_set_cloexec(kq) < 0) ||
        ((flags & LINUX_TFD_NONBLOCK) && fd_set_nonblock(kq) < 0)) {
        close(kq);
        return linux_errno();
    }

    int gfd = fd_alloc(FD_TIMERFD, kq, timerfd_close);
    if (gfd < 0) {
        close(kq);
        return -LINUX_EMFILE;
    }

    pthread_mutex_lock(&sfd_lock);
    int slot = timerfd_alloc();
    if (slot < 0) {
        pthread_mutex_unlock(&sfd_lock);
        fd_mark_closed(gfd);
        close(kq);
        return -LINUX_ENOMEM;
    }

    memset(&timerfd_state[slot], 0, sizeof(timerfd_state[slot]));
    timerfd_state[slot].guest_fd = gfd;
    timerfd_state[slot].kq_fd = kq;
    timerfd_state[slot].clockid = clockid;
    pthread_mutex_unlock(&sfd_lock);

    fd_table[gfd].linux_flags =
        (flags & LINUX_TFD_CLOEXEC) ? LINUX_O_CLOEXEC : 0;
    return gfd;
}

int64_t sys_timerfd_settime(guest_t *g,
                            int fd,
                            int flags,
                            uint64_t new_value_gva,
                            uint64_t old_value_gva)
{
    int64_t ret = 0;

    pthread_mutex_lock(&sfd_lock);
    int slot = timerfd_find(fd);
    if (slot < 0) {
        ret = -LINUX_EBADF;
        goto unlock;
    }

    linux_itimerspec_t its;
    if (guest_read_small(g, new_value_gva, &its, sizeof(its)) < 0) {
        ret = -LINUX_EFAULT;
        goto unlock;
    }

    if (flags & ~(LINUX_TFD_TIMER_ABSTIME | LINUX_TFD_TIMER_CANCEL_ON_SET)) {
        ret = -LINUX_EINVAL;
        goto unlock;
    }

    /* Validate timespec fields before any arithmetic. */
    if (its.it_value_sec < 0 || its.it_interval_sec < 0 ||
        !RANGE_CHECK(its.it_value_nsec, 0, NS_PER_SEC) ||
        !RANGE_CHECK(its.it_interval_nsec, 0, NS_PER_SEC)) {
        ret = -LINUX_EINVAL;
        goto unlock;
    }

    /* Return old value if requested (compute remaining time) */
    if (old_value_gva) {
        linux_itimerspec_t old = {0};
        if (timerfd_state[slot].armed) {
            old.it_interval_sec = timerfd_state[slot].interval_ns / NS_PER_SEC;
            old.it_interval_nsec = timerfd_state[slot].interval_ns % NS_PER_SEC;

            struct timespec now;
            clock_gettime(CLOCK_MONOTONIC, &now);
            int64_t now_ns = now.tv_sec * NS_PER_SEC + now.tv_nsec;
            int64_t remaining = timerfd_remaining_ns_locked(slot, now_ns);
            if (remaining > 0) {
                old.it_value_sec = remaining / NS_PER_SEC;
                old.it_value_nsec = remaining % NS_PER_SEC;
            }
        }
        if (guest_write_small(g, old_value_gva, &old, sizeof(old)) < 0) {
            ret = -LINUX_EFAULT;
            goto unlock;
        }
    }

    int kq = timerfd_state[slot].kq_fd;

    if ((flags & LINUX_TFD_TIMER_ABSTIME) &&
        (its.it_value_sec != 0 || its.it_value_nsec != 0)) {
        struct timespec now;
        int host_clock = timerfd_state[slot].clockid == LINUX_CLOCK_REALTIME
                             ? CLOCK_REALTIME
                             : CLOCK_MONOTONIC;
        clock_gettime(host_clock, &now);
        int64_t target_sec = its.it_value_sec;
        if (target_sec > INT64_MAX / NS_PER_SEC)
            target_sec = INT64_MAX / NS_PER_SEC;
        int64_t target_ns = target_sec * NS_PER_SEC + its.it_value_nsec;
        int64_t now_ns = now.tv_sec * NS_PER_SEC + now.tv_nsec;
        int64_t relative_ns = target_ns > now_ns ? target_ns - now_ns : 1;
        its.it_value_sec = relative_ns / NS_PER_SEC;
        its.it_value_nsec = relative_ns % NS_PER_SEC;
    }

    /* Clamp large seconds values to prevent signed integer overflow.
     * INT64_MAX / 1e6 is about 9.2e12 seconds; INT64_MAX / 1e9 is about 9.2e9
     * seconds.
     */
    int64_t val_sec = its.it_value_sec, int_sec = its.it_interval_sec;
    if (val_sec > INT64_MAX / US_PER_SEC)
        val_sec = INT64_MAX / US_PER_SEC;
    if (int_sec > INT64_MAX / NS_PER_SEC)
        int_sec = INT64_MAX / NS_PER_SEC;
    int64_t value_us = val_sec * US_PER_SEC + its.it_value_nsec / 1000;
    int64_t interval_ns = int_sec * NS_PER_SEC + its.it_interval_nsec;

    log_debug(
        "timerfd_settime: gfd=%d value=%lld.%09lld "
        "interval=%lld.%09lld (value_us=%lld interval_ns=%lld oneshot=%d)",
        fd, (long long) its.it_value_sec, (long long) its.it_value_nsec,
        (long long) its.it_interval_sec, (long long) its.it_interval_nsec,
        (long long) value_us, (long long) interval_ns, interval_ns == 0);

    if (its.it_value_sec == 0 && its.it_value_nsec == 0) {
        /* Disarm timer */
        struct kevent kev;
        EV_SET(&kev, 1, EVFILT_TIMER, EV_DELETE, 0, 0, NULL);
        kevent(kq, &kev, 1, NULL, 0, NULL); /* Ignore error if not armed */

        /* Drain any stale pending events so the next timerfd_read() does not
         * see expirations from the now-disarmed timer.
         */
        struct timespec ts_zero = {0, 0};
        while (kevent(kq, NULL, 0, &kev, 1, &ts_zero) > 0)
            ;

        timerfd_state[slot].armed = false;
        timerfd_state[slot].expirations = 0;
    } else {
        /* Arm timer.  Always use EV_ONESHOT: for repeating timers where initial
         * value != interval, kqueue would repeat at the wrong period (initial
         * instead of interval). timerfd_read re-arms with the interval after
         * the first read.
         */
        struct kevent kev;
        EV_SET(&kev, 1, EVFILT_TIMER, EV_ADD | EV_ENABLE | EV_ONESHOT,
               NOTE_USECONDS, value_us, NULL);
        if (kevent(kq, &kev, 1, NULL, 0, NULL) < 0) {
            ret = linux_errno();
            goto unlock;
        }
        timerfd_state[slot].armed = true;
        timerfd_state[slot].interval_ns = interval_ns;
        /* Use separate clamping for nanosecond computation: val_sec is clamped
         * for microsecond use (INT64_MAX / 1e6), which overflows when * 1e9.
         */
        int64_t init_sec = its.it_value_sec;
        if (init_sec > INT64_MAX / NS_PER_SEC)
            init_sec = INT64_MAX / NS_PER_SEC;
        timerfd_state[slot].initial_ns =
            init_sec * NS_PER_SEC + its.it_value_nsec;
        timerfd_state[slot].expirations = 0;

        /* Record arm time for gettime remaining-time calculation */
        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);
        timerfd_state[slot].arm_time_ns = now.tv_sec * NS_PER_SEC + now.tv_nsec;
    }

unlock:
    pthread_mutex_unlock(&sfd_lock);
    return ret;
}

int64_t sys_timerfd_gettime(guest_t *g, int fd, uint64_t curr_value_gva)
{
    pthread_mutex_lock(&sfd_lock);
    int slot = timerfd_find(fd);
    if (slot < 0) {
        pthread_mutex_unlock(&sfd_lock);
        return -LINUX_EBADF;
    }

    linux_itimerspec_t its = {0};
    if (timerfd_state[slot].armed) {
        its.it_interval_sec = timerfd_state[slot].interval_ns / NS_PER_SEC;
        its.it_interval_nsec = timerfd_state[slot].interval_ns % NS_PER_SEC;

        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);
        int64_t now_ns = now.tv_sec * NS_PER_SEC + now.tv_nsec;
        int64_t remaining = timerfd_remaining_ns_locked(slot, now_ns);

        if (remaining <= 0) {
            /* Timer already expired (one-shot) */
            its.it_value_sec = 0;
            its.it_value_nsec = 0;
        } else {
            its.it_value_sec = remaining / NS_PER_SEC;
            its.it_value_nsec = remaining % NS_PER_SEC;
        }
    }
    pthread_mutex_unlock(&sfd_lock);
    if (guest_write_small(g, curr_value_gva, &its, sizeof(its)) < 0)
        return -LINUX_EFAULT;
    return 0;
}

/* Read from timerfd: collect pending timer events from the kqueue, return
 * accumulated expiration count as uint64_t. Resets count to 0 after read (Linux
 * timerfd semantics).
 */
int64_t timerfd_read(int guest_fd, guest_t *g, uint64_t buf_gva, uint64_t count)
{
    if (count < 8)
        return -LINUX_EINVAL;

    pthread_mutex_lock(&sfd_lock);
    int slot = timerfd_find(guest_fd);
    if (slot < 0) {
        pthread_mutex_unlock(&sfd_lock);
        return -LINUX_EBADF;
    }

    int kq = timerfd_state[slot].kq_fd;

    /* Collect pending timer events into the slot's accumulator. */
    timerfd_drain_pending_locked(slot);

    if (timerfd_state[slot].expirations == 0) {
        /* No events yet; check if non-blocking */
        int fl = fcntl(kq, F_GETFL);
        if (fl >= 0 && (fl & O_NONBLOCK)) {
            pthread_mutex_unlock(&sfd_lock);
            return -LINUX_EAGAIN;
        }

        /* If the timer was never armed, there's no kevent registered --
         * kevent(NULL timeout) would block forever. Return EAGAIN.
         */
        if (!timerfd_state[slot].armed) {
            pthread_mutex_unlock(&sfd_lock);
            return -LINUX_EAGAIN;
        }

        /* Blocking: release lock, wait for the timer, re-lock.
         * Another thread may close the fd while the timerfd wait is active --
         * kevent() returns EBADF in that case, and the code re-validates the
         * slot.
         */
        struct kevent kev;
        pthread_mutex_unlock(&sfd_lock);
        int nev = kevent(kq, NULL, 0, &kev, 1, NULL);
        pthread_mutex_lock(&sfd_lock);
        /* Re-validate: slot may have been freed by timerfd_close() */
        if (timerfd_state[slot].guest_fd != guest_fd) {
            pthread_mutex_unlock(&sfd_lock);
            return -LINUX_EBADF;
        }
        if (nev > 0) {
            uint64_t fires = (uint64_t) kev.data;
            if (fires == 0)
                fires = 1;
            timerfd_state[slot].expirations += fires;
        }
        if (timerfd_state[slot].expirations == 0) {
            pthread_mutex_unlock(&sfd_lock);
            return -LINUX_EAGAIN;
        }
    }

    uint64_t val = timerfd_state[slot].expirations;
    timerfd_state[slot].expirations = 0;
    bool armed = timerfd_state[slot].armed;
    int64_t intv = timerfd_state[slot].interval_ns;
    int rearm_kq = timerfd_state[slot].kq_fd;

    /* Re-arm repeating timers with the interval period.  The initial fire used
     * EV_ONESHOT (see timerfd_settime), so the code re-arms here to get correct
     * interval timing even when initial != interval.
     */
    if (intv > 0 && armed) {
        int64_t interval_us = intv / 1000;
        if (interval_us == 0)
            interval_us = 1; /* Minimum 1us */
        struct kevent rearm_kev;
        EV_SET(&rearm_kev, 1, EVFILT_TIMER, EV_ADD | EV_ENABLE | EV_ONESHOT,
               NOTE_USECONDS, interval_us, NULL);
        kevent(rearm_kq, &rearm_kev, 1, NULL, 0, NULL);

        /* Update arm time for gettime remaining-time calculation */
        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);
        timerfd_state[slot].arm_time_ns = now.tv_sec * NS_PER_SEC + now.tv_nsec;
        timerfd_state[slot].initial_ns = intv; /* Next fire is one interval */
    }
    pthread_mutex_unlock(&sfd_lock);

    log_debug("timerfd_read: gfd=%d expirations=%llu armed=%d interval_ns=%lld",
              guest_fd, (unsigned long long) val, armed, (long long) intv);

    if (guest_write_small(g, buf_gva, &val, sizeof(val)) < 0)
        return -LINUX_EFAULT;

    return 8;
}

/* Clean up timerfd state when guest closes the fd. Must hold sfd_lock to
 * prevent racing with concurrent timerfd_read.
 */
static void timerfd_close(int guest_fd)
{
    pthread_mutex_lock(&sfd_lock);
    int slot = timerfd_find(guest_fd);
    if (slot >= 0) {
        /* kq_fd is closed by sys_close() as host_fd */
        timerfd_state[slot].guest_fd = -1;
        timerfd_state[slot].expirations = 0;
        timerfd_state[slot].armed = false;
    }
    pthread_mutex_unlock(&sfd_lock);
}

/* eventfd emulation via pipe + counter
 *
 * Linux eventfd is a semaphore-like fd: writes add to a uint64 counter, reads
 * return the counter and reset it to zero. EFD_SEMAPHORE mode returns 1 per
 * read and decrements. Eventfd emulation uses a self-pipe for poll/epoll
 * compatibility plus a separate counter.
 */

/* Linux eventfd flags */
#define LINUX_EFD_CLOEXEC 0x80000 /* Same as O_CLOEXEC on aarch64 */
#define LINUX_EFD_NONBLOCK 0x800  /* Same as O_NONBLOCK */
#define LINUX_EFD_SEMAPHORE 1

/* Per-eventfd state */
#define EVENTFD_MAX 32
static struct {
    int guest_fd;     /* Guest fd (-1 if unused) */
    int pipe_rd;      /* Read end of self-pipe (for poll/epoll readiness) */
    int pipe_wr;      /* Write end of self-pipe */
    uint64_t counter; /* Accumulated event counter */
    int semaphore;    /* EFD_SEMAPHORE mode */
    int nonblock;     /* O_NONBLOCK */
} eventfd_state[EVENTFD_MAX];

void eventfd_init(void)
{
    for (int i = 0; i < EVENTFD_MAX; i++)
        eventfd_state[i].guest_fd = -1;
}

static int eventfd_find(int guest_fd)
{
    return sfd_find_slot(eventfd_state, EVENTFD_MAX, sizeof(eventfd_state[0]),
                         guest_fd);
}

static int eventfd_slot_alloc(void)
{
    return sfd_alloc_slot(eventfd_state, EVENTFD_MAX, sizeof(eventfd_state[0]));
}

int64_t sys_eventfd2(unsigned int initval, int flags)
{
    if (flags & ~(LINUX_EFD_CLOEXEC | LINUX_EFD_NONBLOCK | LINUX_EFD_SEMAPHORE))
        return -LINUX_EINVAL;

    /* Create self-pipe for poll/epoll readiness signaling */
    int pipefd[2];
    if (pipe(pipefd) < 0)
        return linux_errno();

    /* Both ends must be non-blocking to avoid blocking on pipe I/O. CLOEXEC
     * is opt-in via EFD_CLOEXEC.
     */
    bool want_cloexec = (flags & LINUX_EFD_CLOEXEC) != 0;
    if (fd_set_nonblock(pipefd[0]) < 0 || fd_set_nonblock(pipefd[1]) < 0 ||
        (want_cloexec &&
         (fd_set_cloexec(pipefd[0]) < 0 || fd_set_cloexec(pipefd[1]) < 0))) {
        close(pipefd[0]);
        close(pipefd[1]);
        return linux_errno();
    }

    /* Allocate guest fd: use read end as the host fd so epoll/poll sees it */
    int gfd = fd_alloc(FD_EVENTFD, pipefd[0], eventfd_close);
    if (gfd < 0) {
        close(pipefd[0]);
        close(pipefd[1]);
        return -LINUX_EMFILE;
    }

    pthread_mutex_lock(&sfd_lock);
    int slot = eventfd_slot_alloc();
    if (slot < 0) {
        pthread_mutex_unlock(&sfd_lock);
        fd_mark_closed(gfd);
        close(pipefd[0]);
        close(pipefd[1]);
        return -LINUX_ENOMEM;
    }

    eventfd_state[slot].guest_fd = gfd;
    eventfd_state[slot].pipe_rd = pipefd[0];
    eventfd_state[slot].pipe_wr = pipefd[1];
    eventfd_state[slot].counter = (uint64_t) initval;
    eventfd_state[slot].semaphore = (flags & LINUX_EFD_SEMAPHORE) ? 1 : 0;
    eventfd_state[slot].nonblock = (flags & LINUX_EFD_NONBLOCK) ? 1 : 0;
    pthread_mutex_unlock(&sfd_lock);

    fd_table[gfd].linux_flags =
        (flags & LINUX_EFD_CLOEXEC) ? LINUX_O_CLOEXEC : 0;

    /* If initial counter > 0, make the pipe readable so poll sees it */
    if (initval > 0) {
        uint8_t byte = 1;
        write(pipefd[1], &byte, 1);
    }

    return gfd;
}

/* Clean up eventfd state when guest closes the fd. Must hold sfd_lock to
 * prevent racing with concurrent eventfd_write/eventfd_read.
 */
static void eventfd_close(int guest_fd)
{
    pthread_mutex_lock(&sfd_lock);
    int slot = eventfd_find(guest_fd);
    if (slot >= 0) {
        close(eventfd_state[slot].pipe_wr);
        /* pipe_rd is closed by sys_close() as host_fd */
        eventfd_state[slot].guest_fd = -1;
        eventfd_state[slot].counter = 0;
    }
    pthread_mutex_unlock(&sfd_lock);
}

/* Read from eventfd: return 8-byte counter value, then reset to 0.
 * In EFD_SEMAPHORE mode, return 1 and decrement counter by 1.
 */
int64_t eventfd_read(int guest_fd, guest_t *g, uint64_t buf_gva, uint64_t count)
{
    if (count < 8)
        return -LINUX_EINVAL;

    pthread_mutex_lock(&sfd_lock);
    int slot = eventfd_find(guest_fd);
    if (slot < 0) {
        pthread_mutex_unlock(&sfd_lock);
        return -LINUX_EBADF;
    }

    if (eventfd_state[slot].counter == 0) {
        if (eventfd_state[slot].nonblock) {
            pthread_mutex_unlock(&sfd_lock);
            return -LINUX_EAGAIN;
        }

        /* Blocking mode: release lock, block on pipe, re-lock.
         * The pipe is O_NONBLOCK, so temporarily make it blocking so read()
         * actually waits for the writer. Matches signalfd_read. Another thread
         * may close the fd while the eventfd wait is active; read() returns
         * EBADF in that case, and the code re-validates the slot.
         */
        int rd_fd = eventfd_state[slot].pipe_rd;
        pthread_mutex_unlock(&sfd_lock);

        uint8_t byte;
        fd_update_status_flag(rd_fd, O_NONBLOCK,
                              false); /* Make temporarily blocking */
        ssize_t r = read(rd_fd, &byte, 1);
        fd_set_nonblock(rd_fd); /* Restore non-blocking */
        if (r < 0)
            return linux_errno();

        pthread_mutex_lock(&sfd_lock);
        /* Re-validate: slot may have been freed by eventfd_close() */
        if (eventfd_state[slot].guest_fd != guest_fd) {
            pthread_mutex_unlock(&sfd_lock);
            return -LINUX_EBADF;
        }
        /* Counter was updated by the writer; re-check */
        if (eventfd_state[slot].counter == 0) {
            pthread_mutex_unlock(&sfd_lock);
            return -LINUX_EAGAIN;
        }
    }

    uint64_t val;
    if (eventfd_state[slot].semaphore) {
        val = 1;
        eventfd_state[slot].counter--;
    } else {
        val = eventfd_state[slot].counter;
        eventfd_state[slot].counter = 0;
    }

    /* Drain pipe readability if counter is now 0 */
    if (eventfd_state[slot].counter == 0) {
        uint8_t drain;
        while (read(eventfd_state[slot].pipe_rd, &drain, 1) > 0)
            ;
    }
    pthread_mutex_unlock(&sfd_lock);

    if (guest_write_small(g, buf_gva, &val, sizeof(val)) < 0)
        return -LINUX_EFAULT;

    return 8;
}

/* Write to eventfd: add value to counter. Value must be uint64_t.
 * Maximum counter value is UINT64_MAX - 1.
 */
int64_t eventfd_write(int guest_fd,
                      guest_t *g,
                      uint64_t buf_gva,
                      uint64_t count)
{
    if (count < 8)
        return -LINUX_EINVAL;

    uint64_t val;
    if (guest_read_small(g, buf_gva, &val, sizeof(val)) < 0)
        return -LINUX_EFAULT;
    if (val == UINT64_MAX)
        return -LINUX_EINVAL;

    pthread_mutex_lock(&sfd_lock);
    int slot = eventfd_find(guest_fd);
    if (slot < 0) {
        pthread_mutex_unlock(&sfd_lock);
        return -LINUX_EBADF;
    }

    /* Check for counter overflow (Linux max is UINT64_MAX - 1) */
    if (eventfd_state[slot].counter > UINT64_MAX - 1 - val) {
        /* Would overflow: block or return EAGAIN. In blocking mode a
         * real kernel blocks until a read drains the counter; the code returns
         * EAGAIN to avoid hanging since eventfd emulation cannot truly block
         * here.
         */
        pthread_mutex_unlock(&sfd_lock);
        return -LINUX_EAGAIN;
    }

    bool was_zero = (eventfd_state[slot].counter == 0);
    eventfd_state[slot].counter += val;

    /* Signal readability via pipe if counter transitioned from 0.
     * The pipe is non-blocking; retry on EINTR and warn on other errors
     * since a missed wakeup here can deadlock ppoll/epoll waiters.
     */
    if (was_zero && eventfd_state[slot].counter > 0) {
        uint8_t byte = 1;
        ssize_t wr;
        do {
            wr = write(eventfd_state[slot].pipe_wr, &byte, 1);
        } while (wr < 0 && errno == EINTR);
        if (wr < 0)
            log_error(
                "eventfd_write: pipe write failed: %s (gfd=%d pipe_wr=%d)",
                strerror(errno), guest_fd, eventfd_state[slot].pipe_wr);
    }
    /* Always log eventfd writes, since this is critical for diagnosing shutdown
     * hangs
     */
    log_debug(
        "eventfd_write: gfd=%d val=%llu counter=%llu was_zero=%d pipe_wr=%d",
        guest_fd, (unsigned long long) val,
        (unsigned long long) eventfd_state[slot].counter, was_zero,
        eventfd_state[slot].pipe_wr);
    pthread_mutex_unlock(&sfd_lock);

    return 8;
}

/* signalfd emulation
 *
 * Linux signalfd creates an fd from which pending signals can be read
 * as signalfd_siginfo structures (128 bytes each). Signalfd integrates with
 * the existing signal_state infrastructure; reads consume pending
 * signals that match the signalfd's mask.
 */

/* Linux signalfd_siginfo structure (128 bytes) */
typedef struct {
    uint32_t ssi_signo;
    int32_t ssi_errno, ssi_code;
    uint32_t ssi_pid, ssi_uid;
    int32_t ssi_fd;
    uint32_t ssi_tid, ssi_band, ssi_overrun, ssi_trapno;
    int32_t ssi_status, ssi_int;
    uint64_t ssi_ptr, ssi_utime, ssi_stime, ssi_addr;
    uint16_t ssi_addr_lsb, __pad2;
    int32_t ssi_syscall;
    uint64_t ssi_call_addr;
    uint32_t ssi_arch;
    uint8_t __pad[28];
} linux_signalfd_siginfo_t;

_Static_assert(sizeof(linux_signalfd_siginfo_t) == 128,
               "signalfd_siginfo must be 128 bytes");

/* Linux SFD_* flags (same values as O_* on aarch64) */
#define LINUX_SFD_CLOEXEC 0x80000
#define LINUX_SFD_NONBLOCK 0x800

/* Per-signalfd state */
#define SIGNALFD_MAX 16
static struct {
    int guest_fd;  /* Guest fd (-1 if unused) */
    int pipe_rd;   /* Read end for poll/epoll readiness */
    int pipe_wr;   /* Write end for signaling */
    uint64_t mask; /* Signal mask (bitmask of signals to accept) */
    int nonblock;  /* O_NONBLOCK */
} signalfd_state[SIGNALFD_MAX];

void signalfd_init(void)
{
    for (int i = 0; i < SIGNALFD_MAX; i++)
        signalfd_state[i].guest_fd = -1;
}

static int signalfd_find(int guest_fd)
{
    return sfd_find_slot(signalfd_state, SIGNALFD_MAX,
                         sizeof(signalfd_state[0]), guest_fd);
}

static int signalfd_slot_alloc(void)
{
    return sfd_alloc_slot(signalfd_state, SIGNALFD_MAX,
                          sizeof(signalfd_state[0]));
}

/* Clean up signalfd state when guest closes the fd. Must hold sfd_lock
 * to prevent racing with concurrent signalfd_notify.
 */
static void signalfd_close(int guest_fd)
{
    pthread_mutex_lock(&sfd_lock);
    int slot = signalfd_find(guest_fd);
    if (slot >= 0) {
        close(signalfd_state[slot].pipe_wr);
        /* pipe_rd is closed by sys_close() as host_fd */
        signalfd_state[slot].guest_fd = -1;
        signalfd_state[slot].mask = 0;
    }
    pthread_mutex_unlock(&sfd_lock);
}

int64_t sys_signalfd4(guest_t *g,
                      int fd,
                      uint64_t mask_gva,
                      uint64_t sigsetsize,
                      int flags)
{
    if (flags & ~(LINUX_SFD_CLOEXEC | LINUX_SFD_NONBLOCK))
        return -LINUX_EINVAL;

    /* Read the signal mask from guest memory */
    uint64_t mask = 0;
    if (sigsetsize < 8)
        return -LINUX_EINVAL;
    if (guest_read_small(g, mask_gva, &mask, sizeof(mask)) < 0)
        return -LINUX_EFAULT;

    /* If fd >= 0, update existing signalfd mask */
    if (fd >= 0) {
        pthread_mutex_lock(&sfd_lock);
        int slot = signalfd_find(fd);
        if (slot < 0) {
            pthread_mutex_unlock(&sfd_lock);
            return -LINUX_EINVAL;
        }
        signalfd_state[slot].mask = mask;
        pthread_mutex_unlock(&sfd_lock);
        return fd;
    }

    /* Create new signalfd */
    int pipefd[2];
    if (pipe(pipefd) < 0)
        return linux_errno();

    bool want_cloexec = (flags & LINUX_SFD_CLOEXEC) != 0;
    if (fd_set_nonblock(pipefd[0]) < 0 || fd_set_nonblock(pipefd[1]) < 0 ||
        (want_cloexec &&
         (fd_set_cloexec(pipefd[0]) < 0 || fd_set_cloexec(pipefd[1]) < 0))) {
        close(pipefd[0]);
        close(pipefd[1]);
        return linux_errno();
    }

    int gfd = fd_alloc(FD_SIGNALFD, pipefd[0], signalfd_close);
    if (gfd < 0) {
        close(pipefd[0]);
        close(pipefd[1]);
        return -LINUX_EMFILE;
    }

    pthread_mutex_lock(&sfd_lock);
    int slot = signalfd_slot_alloc();
    if (slot < 0) {
        pthread_mutex_unlock(&sfd_lock);
        fd_mark_closed(gfd);
        close(pipefd[0]);
        close(pipefd[1]);
        return -LINUX_ENOMEM;
    }

    signalfd_state[slot].guest_fd = gfd;
    signalfd_state[slot].pipe_rd = pipefd[0];
    signalfd_state[slot].pipe_wr = pipefd[1];
    signalfd_state[slot].mask = mask;
    signalfd_state[slot].nonblock = (flags & LINUX_SFD_NONBLOCK) ? 1 : 0;
    pthread_mutex_unlock(&sfd_lock);

    fd_table[gfd].linux_flags =
        (flags & LINUX_SFD_CLOEXEC) ? LINUX_O_CLOEXEC : 0;

    return gfd;
}

/* Read from signalfd: consume pending signals matching the signalfd's mask.
 *
 * Each signal produces one signalfd_siginfo (128 bytes). RT signals (32-64)
 * are queued: each sigqueue/rt_tgsigqueueinfo enqueues a distinct instance with
 * its own si_int/si_ptr payload, and signalfd_read returns them in FIFO order
 * without coalescing (Linux behavior).
 *
 * Per-thread signal mask is intentionally not consulted: signalfd is the
 * standard mechanism for reading signals that were blocked from synchronous
 * delivery via sigprocmask(). The signalfd's own mask (set at create time or
 * via signalfd(fd, &mask, ...)) is the only filter applied.
 *
 * ssi_int/ssi_ptr are populated from queued metadata when present.
 * Standard signals (1-31) still coalesce to one pending instance, but Linux
 * preserves one siginfo payload for that instance.
 *
 * Returns the number of bytes read (multiple of sizeof(signalfd_siginfo)), or
 * -EAGAIN if nothing pending and the fd is non-blocking.
 */
int64_t signalfd_read(int guest_fd,
                      guest_t *g,
                      uint64_t buf_gva,
                      uint64_t count)
{
retry:
    /* Capture slot state under sfd_lock, then release BEFORE calling
     * signal_get_state() which acquires sig_lock(4). Holding sfd_lock(5a)
     * while taking sig_lock(4) would violate lock ordering.
     */
    pthread_mutex_lock(&sfd_lock);
    int slot = signalfd_find(guest_fd);
    if (slot < 0) {
        pthread_mutex_unlock(&sfd_lock);
        return -LINUX_EBADF;
    }

    uint64_t mask = signalfd_state[slot].mask;
    int nonblock = signalfd_state[slot].nonblock;
    int pipe_rd = signalfd_state[slot].pipe_rd;
    size_t max_signals = count / sizeof(linux_signalfd_siginfo_t);
    if (max_signals == 0) {
        pthread_mutex_unlock(&sfd_lock);
        return -LINUX_EINVAL;
    }
    pthread_mutex_unlock(&sfd_lock);

    signal_rt_info_t pending_stack[LINUX_NSIG];
    signal_rt_info_t *pending = pending_stack;
    size_t total = 0;
    const signal_state_t *sig = signal_get_state();
    /* Match pending signals against the signalfd mask. Do NOT filter by
     * sig->blocked because signalfd is specifically designed to read signals
     * that were blocked from normal delivery via sigprocmask().
     */
    uint64_t deliverable = sig->pending & mask;

    if (max_signals > LINUX_NSIG) {
        pending = malloc(max_signals * sizeof(*pending));
        if (!pending)
            return -LINUX_ENOMEM;
    }

    if (deliverable == 0) {
        if (nonblock)
            goto no_pending;

        /* Blocking mode: wait for signalfd_notify() to write to the pipe.
         * Re-validate slot after wake.
         */
        uint8_t byte;
        /* pipe_rd is O_NONBLOCK, so temporarily make it blocking for the wait
         */
        fcntl(pipe_rd, F_SETFL, 0);
        ssize_t r = read(pipe_rd, &byte, 1);
        fcntl(pipe_rd, F_SETFL, O_NONBLOCK);
        if (r <= 0)
            goto no_pending;

        /* Re-validate: slot may have been freed by signalfd_close() */
        pthread_mutex_lock(&sfd_lock);
        int still_valid = (signalfd_state[slot].guest_fd == guest_fd);
        pthread_mutex_unlock(&sfd_lock);
        if (!still_valid) {
            if (pending != pending_stack)
                free(pending);
            return -LINUX_EBADF;
        }

        /* Re-check: a signal matching the current mask should now be pending */
        sig = signal_get_state();
        deliverable = sig->pending & mask;
        if (deliverable == 0)
            goto no_pending;
    }
    size_t peeked = signal_peek_signalfd(mask, pending, max_signals);
    if (peeked == 0)
        goto no_pending;

    /* Write-then-take. Writing first means that on a guest_write_small EFAULT
     * the rt-queue is still intact and signals are not lost: no re-queue dance,
     * no RT_SIGQUEUE_MAX overflow window, no extra signalfd_notify writes that
     * would desync the pipe-byte count from the actual pending-signal count.
     * Take only the prefix the writer landed; if a concurrent consumer advanced
     * the rt-queue head between peek and take, take returns less than the
     * written count and the bridge restarts the read loop via the retry label
     * below.
     */
    size_t written = 0;
    for (size_t i = 0; i < peeked; i++) {
        linux_signalfd_siginfo_t info;
        memset(&info, 0, sizeof(info));
        info.ssi_signo = (uint32_t) pending[i].signum;
        info.ssi_code = pending[i].si_code;
        info.ssi_pid = (uint32_t) pending[i].si_pid;
        info.ssi_uid = pending[i].si_uid;
        info.ssi_int = pending[i].si_int;
        info.ssi_ptr = pending[i].si_ptr;

        uint64_t off = i * sizeof(linux_signalfd_siginfo_t);
        if (guest_write_small(g, buf_gva + off, &info, sizeof(info)) < 0) {
            if (written == 0) {
                /* No bytes transferred: surface EFAULT, leave the queue
                 * untouched so the signal is not lost. Matches the elfuse
                 * promise locked in by tests/test-tier-b's
                 * test_signalfd_efault_preserves_pending.
                 */
                if (pending != pending_stack)
                    free(pending);
                return -LINUX_EFAULT;
            }

            /* Partial success: stop writing and let take consume only the
             * delivered prefix. The unwritten entries stay in the rt-queue
             * naturally because the take call has not run yet.
             */
            break;
        }
        written++;
    }

    total = signal_take_signalfd_exact(pending, written);
    if (total == 0) {
        if (written == 0)
            goto no_pending;
        if (pending != pending_stack)
            free(pending);
        goto retry;
    }

    /* Drain pipe: consume exactly one byte per signal read. If the code drains
     * ALL bytes, the code would lose notifications for signals that arrived
     * between sfd_lock release and this drain. Draining the consumed count
     * preserves new notifications.
     */
    for (size_t i = 0; i < total; i++) {
        uint8_t drain;
        if (read(pipe_rd, &drain, 1) <= 0)
            break;
    }

    if (pending != pending_stack)
        free(pending);
    return (int64_t) total * (int64_t) sizeof(linux_signalfd_siginfo_t);

no_pending:
    if (pending != pending_stack)
        free(pending);
    return -LINUX_EAGAIN;
}

/* Notify signalfd pipes when a signal is queued. Called from signal_queue();
 * writes a byte to make poll/epoll see readability.
 */
void signalfd_notify(int signum)
{
    pthread_mutex_lock(&sfd_lock);
    for (int i = 0; i < SIGNALFD_MAX; i++) {
        if (signalfd_state[i].guest_fd < 0)
            continue;

        uint64_t bit = BIT64(signum - 1);
        if (signalfd_state[i].mask & bit) {
            uint8_t byte = 1;
            write(signalfd_state[i].pipe_wr, &byte, 1);
        }
    }
    pthread_mutex_unlock(&sfd_lock);
}

/* /proc/self/fdinfo type-specific snapshots. Each takes sfd_lock to prevent
 * tearing across concurrent read/write/settime; lock order is fd_lock(3)
 * -> sfd_lock(5a), and these accessors take only sfd_lock so the procemu
 * caller is free to drop fd_lock between fd_snapshot and the lookup here.
 */

bool eventfd_fdinfo_snapshot(int guest_fd, uint64_t *count_out)
{
    pthread_mutex_lock(&sfd_lock);
    int slot = eventfd_find(guest_fd);
    if (slot < 0) {
        pthread_mutex_unlock(&sfd_lock);
        return false;
    }
    *count_out = eventfd_state[slot].counter;
    pthread_mutex_unlock(&sfd_lock);
    return true;
}

bool signalfd_fdinfo_snapshot(int guest_fd, uint64_t *mask_out)
{
    pthread_mutex_lock(&sfd_lock);
    int slot = signalfd_find(guest_fd);
    if (slot < 0) {
        pthread_mutex_unlock(&sfd_lock);
        return false;
    }
    *mask_out = signalfd_state[slot].mask;
    pthread_mutex_unlock(&sfd_lock);
    return true;
}

bool timerfd_fdinfo_snapshot(int guest_fd,
                             int *clockid_out,
                             uint64_t *ticks_out,
                             int64_t *value_ns_out,
                             int64_t *interval_ns_out)
{
    pthread_mutex_lock(&sfd_lock);
    int slot = timerfd_find(guest_fd);
    if (slot < 0) {
        pthread_mutex_unlock(&sfd_lock);
        return false;
    }
    /* Fold any pending kqueue fires into expirations before exporting,
     * matching what timerfd_read does. Without this, fdinfo lags by
     * however many ticks were sitting on the kqueue.
     */
    timerfd_drain_pending_locked(slot);
    *clockid_out = timerfd_state[slot].clockid;
    *ticks_out = timerfd_state[slot].expirations;
    *interval_ns_out = timerfd_state[slot].interval_ns;
    int64_t value_ns = 0;
    if (timerfd_state[slot].armed) {
        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);
        int64_t now_ns = (int64_t) now.tv_sec * NS_PER_SEC + now.tv_nsec;
        value_ns = timerfd_remaining_ns_locked(slot, now_ns);
    }
    *value_ns_out = value_ns;
    pthread_mutex_unlock(&sfd_lock);
    return true;
}
