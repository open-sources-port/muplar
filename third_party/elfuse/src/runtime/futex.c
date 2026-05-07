/* Linux futex emulation
 *
 * Copyright 2026 elfuse contributors
 * Copyright 2025 Moritz Angermann, zw3rk pte. ltd.
 * SPDX-License-Identifier: Apache-2.0
 *
 * Hash table of wait queues keyed by guest virtual address. Each bucket has its
 * own mutex for fine-grained locking. Waiters are singly-linked lists with
 * per-waiter condition variables for precise wakeup.
 *
 * Atomicity: The critical FUTEX_WAIT race (guest writes futex word after the
 * current read but before the waiter sleeps) is prevented by holding the bucket
 * lock across the word-read + enqueue + cond_wait sequence. FUTEX_WAKE also
 * acquires the same bucket lock, so a wake cannot slip between the read and the
 * wait.
 *
 * Timeout: FUTEX_WAIT with a non-NULL timeout uses pthread_cond_timedwait with
 * an absolute deadline. FUTEX_WAIT_BITSET always uses absolute time.
 */

#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>

#include "utils.h"

#include "runtime/futex.h"
#include "runtime/thread.h"

#include "syscall/abi.h"
#include "syscall/proc.h"

/* Interrupt flag: when set, futex_wait returns -EINTR. Used to simulate SIGCHLD
 * delivery when all CLONE_THREAD workers exit: wakes the main thread from
 * blocking futex_wait without triggering a full exit_group.
 */
static _Atomic int futex_interrupt_requested = 0;

/* Futex operations (from Linux uapi). */
#define FUTEX_WAIT 0
#define FUTEX_WAKE 1
#define FUTEX_REQUEUE 3
#define FUTEX_CMP_REQUEUE 4
#define FUTEX_WAKE_OP 5
#define FUTEX_LOCK_PI 6
#define FUTEX_UNLOCK_PI 7
#define FUTEX_TRYLOCK_PI 8
#define FUTEX_WAIT_BITSET 9
#define FUTEX_WAKE_BITSET 10

/* Strips the FUTEX_PRIVATE_FLAG (0x80) and FUTEX_CLOCK_REALTIME bits so the
 * dispatch switch sees only the base operation. Emulation does not
 * differentiate private vs shared futexes (single-process guest).
 */
#define FUTEX_CMD_MASK 0x7F

#define FUTEX_BITSET_MATCH_ANY 0xFFFFFFFFU

/* PI futex word layout (bits):
 *   0-30: TID of lock holder (0 = unlocked)
 *   31:   FUTEX_WAITERS (at least one thread is blocked)
 *
 * Linux kernel: FUTEX_WAITERS=0x80000000 (bit 31),
 * FUTEX_OWNER_DIED=0x40000000 (bit 30), FUTEX_TID_MASK=0x3FFFFFFF.
 * FUTEX_OWNER_DIED=0x40000000 (bit 30) is set by robust_list_walk
 * on thread exit. FUTEX_TID_MASK is 30 bits.
 */
#define FUTEX_TID_MASK 0x3FFFFFFFU
#define FUTEX_OWNER_DIED 0x40000000U
#define FUTEX_WAITERS 0x80000000U

/* Hash table. */

#define FUTEX_BUCKETS 64

/* Per-waiter node. Allocated on the host stack of the waiting thread (no malloc
 * needed; the waiter is stack-local to sys_futex).
 *
 * group_lock / group_cond are optional: when non-NULL, a wake additionally
 * signals group_cond under group_lock. futex_waitv uses this so that any wake
 * across the wait set unblocks the polling thread without per-bucket polling.
 */
typedef struct futex_waiter {
    uint64_t uaddr;            /* Guest VA being waited on */
    uint32_t bitset;           /* For WAIT_BITSET matching */
    pthread_cond_t cond;       /* Signalled by WAKE to unblock this waiter */
    int woken;                 /* Set to 1 by WAKE before signalling */
    struct futex_waiter *next; /* Next waiter in same bucket */
    pthread_mutex_t *group_lock;
    pthread_cond_t *group_cond;
} futex_waiter_t;

/* If the waiter belongs to a futex_waitv group, signal the group's cond so the
 * polling thread wakes immediately. Caller holds the bucket lock; group_lock is
 * acquired below it (lock order: bucket -> group_lock).
 */
static void futex_waiter_notify_group(futex_waiter_t *w)
{
    if (!w->group_cond)
        return;
    pthread_mutex_lock(w->group_lock);
    pthread_cond_signal(w->group_cond);
    pthread_mutex_unlock(w->group_lock);
}

/* One bucket in the hash table. Protected by its own mutex.
 * Lock order: 7 (leaf locks, index-ordered when two acquired).
 */
typedef struct {
    pthread_mutex_t lock;
    futex_waiter_t *head; /* Linked list of waiters hashing to this bucket */
} futex_bucket_t;

static futex_bucket_t buckets[FUTEX_BUCKETS];

/* Hash a guest VA to a bucket index. Futex addresses are typically 4-byte
 * aligned, so the low bits carry no entropy; shift them off and XOR a higher
 * slice in to spread aligned addresses across buckets.
 */
static inline unsigned futex_hash(uint64_t uaddr)
{
    return (unsigned) ((uaddr >> 2) ^ (uaddr >> 14)) % FUTEX_BUCKETS;
}

/* Unlink a waiter from its bucket's singly-linked list. Caller must hold
 * b->lock. Silently returns if the waiter is not in the list (already
 * unlinked by a wake/requeue).
 */
static void bucket_unlink_locked(futex_bucket_t *b, const futex_waiter_t *w)
{
    for (futex_waiter_t **pp = &b->head; *pp; pp = &(*pp)->next) {
        if (*pp == w) {
            *pp = w->next;
            return;
        }
    }
}

/* Public API */

void futex_init(void)
{
    for (int i = 0; i < FUTEX_BUCKETS; i++) {
        pthread_mutex_init(&buckets[i].lock, NULL);
        buckets[i].head = NULL;
    }
}

void futex_interrupt_request(void)
{
    atomic_store(&futex_interrupt_requested, 1);
}

void futex_interrupt_clear(void)
{
    atomic_store(&futex_interrupt_requested, 0);
}

int futex_interrupt_pending(void)
{
    return atomic_load(&futex_interrupt_requested);
}

/* Cap on guest-supplied tv_sec. The cap exists purely so the int64_t / time_t
 * arithmetic in the deadline conversion (now.tv_sec + delta_sec, where
 * delta_sec = lts.tv_sec - mono.tv_sec) cannot overflow even for adversarial
 * inputs. INT64_MAX / 4 leaves four-way headroom for any pairwise sum or
 * difference and still allows absolute CLOCK_REALTIME deadlines billions of
 * years into the future, which comfortably covers the year-2038/2106
 * envelope. Linux saturates at KTIME_MAX (INT64_MAX ns ~ 292 years) on
 * conversion to ktime_t; this code stays in struct timespec so it does not
 * need that conversion, only the cap.
 */
#define FUTEX_TIMESPEC_SEC_MAX (INT64_MAX / 4)

static int linux_timespec_is_valid(const linux_timespec_t *lts)
{
    return lts->tv_sec >= 0 && lts->tv_sec <= FUTEX_TIMESPEC_SEC_MAX &&
           lts->tv_nsec >= 0 && lts->tv_nsec < 1000000000L;
}

/* Convert a Linux guest timespec to an absolute struct timespec deadline.
 * For FUTEX_WAIT (relative timeout), adds the duration to the current time.
 * For FUTEX_WAIT_BITSET (absolute timeout), uses the value directly.
 * Returns 0 on success, -1 if the guest pointer is invalid, -2 if the guest
 * timespec is malformed.
 */
static int futex_make_deadline(guest_t *g,
                               uint64_t timeout_gva,
                               int is_absolute,
                               struct timespec *out)
{
    linux_timespec_t lts;
    if (guest_read_small(g, timeout_gva, &lts, sizeof(lts)) < 0)
        return -1;
    if (!linux_timespec_is_valid(&lts))
        return -2;

    if (is_absolute) {
        out->tv_sec = (time_t) lts.tv_sec;
        out->tv_nsec = (long) lts.tv_nsec;
    } else {
        /* Relative: add to current CLOCK_REALTIME (pthread_cond_timedwait uses
         * CLOCK_REALTIME by default on macOS)
         */
        struct timeval now;
        gettimeofday(&now, NULL);
        out->tv_sec = now.tv_sec + (time_t) lts.tv_sec;
        out->tv_nsec = (long) now.tv_usec * 1000 + (long) lts.tv_nsec;
        timespec_normalize(out);
    }
    return 0;
}

/* FUTEX_WAIT / FUTEX_WAIT_BITSET: atomically check word == val, then sleep. */
static int64_t futex_wait(guest_t *g,
                          uint64_t uaddr,
                          uint32_t expected,
                          uint64_t timeout_gva,
                          uint32_t bitset,
                          int is_absolute)
{
    if (bitset == 0)
        return -LINUX_EINVAL;

    unsigned idx = futex_hash(uaddr);
    futex_bucket_t *b = &buckets[idx];

    /* Build deadline before locking (avoid holding lock during syscall) */
    bool has_timeout = (timeout_gva != 0);
    struct timespec deadline;
    if (has_timeout) {
        int rc = futex_make_deadline(g, timeout_gva, is_absolute, &deadline);
        if (rc == -1)
            return -LINUX_EFAULT;
        if (rc == -2)
            return -LINUX_EINVAL;
    }

    pthread_mutex_lock(&b->lock);

    /* Atomically read the futex word while holding the bucket lock.
     * If it does not match, return EAGAIN immediately.
     */
    uint32_t *word = (uint32_t *) guest_ptr(g, uaddr);
    if (!word) {
        pthread_mutex_unlock(&b->lock);
        return -LINUX_EFAULT;
    }

    uint32_t current = __atomic_load_n(word, __ATOMIC_SEQ_CST);
    if (current != expected) {
        pthread_mutex_unlock(&b->lock);
        return -LINUX_EAGAIN;
    }

    /* Enqueue waiter (stack-allocated, lives on this thread's stack) */
    futex_waiter_t waiter = {
        .uaddr = uaddr,
        .bitset = bitset,
        .woken = 0,
        .next = b->head,
    };
    pthread_cond_init(&waiter.cond, NULL);
    b->head = &waiter;

    /* Wait until woken or timeout */
    int ret = 0;

    /* Record start time for the no-timeout path. On real Linux, any pending
     * signal interrupts futex_wait with -EINTR. Without a timer signal
     * (SIGVTALRM from timer_create/setitimer), some multi-threaded runtimes
     * can deadlock when a thread blocks in futex_wait and no wakeup arrives
     * (e.g., a shutdown signal delivered to the wrong I/O manager).
     * FUTEX_WAIT returns -EINTR after 1 second of blocking to simulate
     * periodic signal delivery. All real futex callers (musl, glibc, and
     * other managed runtimes) handle -EINTR correctly by re-checking their
     * condition and retrying.
     */
    struct timeval wait_start;
    if (!has_timeout)
        gettimeofday(&wait_start, NULL);

    while (!__atomic_load_n(&waiter.woken, __ATOMIC_ACQUIRE)) {
        if (has_timeout) {
            int rc = pthread_cond_timedwait(&waiter.cond, &b->lock, &deadline);
            if (rc != 0) {
                /* Timeout (ETIMEDOUT) or error; stop waiting */
                ret = -LINUX_ETIMEDOUT;
                break;
            }
        } else {
            /* No timeout specified: poll every 100ms to check for exit_group,
             * futex_interrupt (simulated SIGCHLD), or excessive wait time
             * (simulated signal interruption).
             */
            struct timespec poll_ts;
            timespec_deadline_in_ms(&poll_ts, 100);
            pthread_cond_timedwait(&waiter.cond, &b->lock, &poll_ts);

            if (proc_exit_group_requested() || futex_interrupt_pending()) {
                ret = -LINUX_EINTR;
                break;
            }

            /* Simulate periodic signal delivery: return -EINTR after 1 second
             * of blocking. This prevents deadlocks in multi-threaded runtimes
             * that rely on signal-interrupted futex_wait for scheduler context
             * switching.
             */
            struct timeval now;
            gettimeofday(&now, NULL);
            long elapsed_ms = (now.tv_sec - wait_start.tv_sec) * 1000 +
                              (now.tv_usec - wait_start.tv_usec) / 1000;
            if (elapsed_ms >= 1000) {
                ret = -LINUX_EINTR;
                break;
            }
        }
    }

    /* Dequeue waiter. If woken=1, the wake/requeue operation already unlinked
     * the waiter from the bucket list, so skip dequeue. If woken=0 (timeout /
     * interrupt), the waiter is still in the list and must self-dequeue.
     *
     * For the self-dequeue path: requeue may have moved the waiter to a
     * different bucket (changed waiter.uaddr), so re-hash. Race: between
     * releasing the old bucket lock and acquiring the new one, another requeue
     * can move the waiter again. Loop until the waiter is found and dequeued.
     */
    if (!__atomic_load_n(&waiter.woken, __ATOMIC_ACQUIRE)) {
        for (;;) {
            unsigned dequeue_idx = futex_hash(waiter.uaddr);
            futex_bucket_t *b_dequeue = &buckets[dequeue_idx];
            if (b_dequeue != b) {
                pthread_mutex_unlock(&b->lock);
                pthread_mutex_lock(&b_dequeue->lock);
                b = b_dequeue;
            }
            /* Search for the current waiter in the bucket */
            bool found = false;
            futex_waiter_t **pp = &b->head;
            while (*pp) {
                if (*pp == &waiter) {
                    *pp = waiter.next;
                    found = true;
                    break;
                }
                pp = &(*pp)->next;
            }
            if (found)
                break;
            /* Not found: waiter was requeued again between the current hash
             * computation and lock acquisition. Re-read uaddr and retry.
             */
        }
    }
    pthread_mutex_unlock(&b->lock);
    pthread_cond_destroy(&waiter.cond);

    if (__atomic_load_n(&waiter.woken, __ATOMIC_ACQUIRE))
        return 0;
    return ret;
}

/* FUTEX_WAKE / FUTEX_WAKE_BITSET: wake up to val waiters at uaddr.
 * Woken waiters are unlinked from the bucket list so subsequent operations do
 * not count them as still-sleeping entries.
 */
static int64_t futex_wake(uint64_t uaddr, uint32_t val, uint32_t bitset)
{
    if (bitset == 0)
        return -LINUX_EINVAL;

    unsigned idx = futex_hash(uaddr);
    futex_bucket_t *b = &buckets[idx];
    int woken = 0;

    pthread_mutex_lock(&b->lock);

    futex_waiter_t **pp = &b->head;
    while (*pp && (uint32_t) woken < val) {
        futex_waiter_t *w = *pp;
        if (w->uaddr == uaddr && (w->bitset & bitset) != 0) {
            *pp = w->next; /* Unlink before signaling */
            __atomic_store_n(&w->woken, 1, __ATOMIC_RELEASE);
            pthread_cond_signal(&w->cond);
            futex_waiter_notify_group(w);
            woken++;
        } else {
            pp = &w->next;
        }
    }

    pthread_mutex_unlock(&b->lock);

    return woken;
}

/* FUTEX_REQUEUE / FUTEX_CMP_REQUEUE: wake val waiters at uaddr, then move up to
 * val2 remaining waiters from uaddr to uaddr2.
 *
 * CMP_REQUEUE additionally checks *uaddr == val3 before proceeding; returns
 * -EAGAIN if the comparison fails (stale wakeup avoidance).
 *
 * Musl uses FUTEX_REQUEUE (not CMP) in pthread_cond_timedwait.c for efficient
 * condition variable broadcast, avoiding thundering herd by moving waiters
 * directly to the mutex futex instead of waking them all.
 *
 * Lock ordering: always acquire lower-indexed bucket first to avoid deadlock
 * when source and destination hash to different buckets.
 */
static int64_t futex_requeue(guest_t *g,
                             uint64_t uaddr,
                             uint32_t wake_count,
                             uint32_t requeue_count,
                             uint64_t uaddr2,
                             int do_cmp,
                             uint32_t expected)
{
    unsigned idx_src = futex_hash(uaddr);
    unsigned idx_dst = futex_hash(uaddr2);
    futex_bucket_t *b_src = &buckets[idx_src];
    futex_bucket_t *b_dst = &buckets[idx_dst];

    /* Lock both buckets in consistent order (lower index first) */
    if (idx_src == idx_dst) {
        pthread_mutex_lock(&b_src->lock);
    } else if (idx_src < idx_dst) {
        pthread_mutex_lock(&b_src->lock);
        pthread_mutex_lock(&b_dst->lock);
    } else {
        pthread_mutex_lock(&b_dst->lock);
        pthread_mutex_lock(&b_src->lock);
    }

    /* CMP_REQUEUE: atomically verify *uaddr == expected */
    if (do_cmp) {
        uint32_t *word = (uint32_t *) guest_ptr(g, uaddr);
        if (!word) {
            if (idx_src != idx_dst)
                pthread_mutex_unlock(&b_dst->lock);
            pthread_mutex_unlock(&b_src->lock);
            return -LINUX_EFAULT;
        }
        uint32_t current = __atomic_load_n(word, __ATOMIC_SEQ_CST);
        if (current != expected) {
            if (idx_src != idx_dst)
                pthread_mutex_unlock(&b_dst->lock);
            pthread_mutex_unlock(&b_src->lock);
            return -LINUX_EAGAIN;
        }
    }

    int woken = 0, requeued = 0;

    /* Walk source bucket: wake up to wake_count, requeue up to requeue_count */
    futex_waiter_t **pp = &b_src->head;
    while (*pp) {
        futex_waiter_t *w = *pp;
        if (w->uaddr != uaddr) {
            pp = &w->next;
            continue;
        }

        if ((uint32_t) woken < wake_count) {
            /* Wake this waiter: unlink from source, then signal */
            *pp = w->next;
            __atomic_store_n(&w->woken, 1, __ATOMIC_RELEASE);
            pthread_cond_signal(&w->cond);
            futex_waiter_notify_group(w);
            woken++;
            /* Leave pp unchanged because *pp is already the next node */
        } else if ((uint32_t) requeued < requeue_count) {
            /* Requeue: remove from source, add to destination */
            *pp = w->next;
            w->uaddr = uaddr2;
            w->next = b_dst->head;
            b_dst->head = w;
            requeued++;
        } else {
            break; /* Both limits reached */
        }
    }

    /* Unlock in reverse order */
    if (idx_src == idx_dst) {
        pthread_mutex_unlock(&b_src->lock);
    } else if (idx_src < idx_dst) {
        pthread_mutex_unlock(&b_dst->lock);
        pthread_mutex_unlock(&b_src->lock);
    } else {
        pthread_mutex_unlock(&b_src->lock);
        pthread_mutex_unlock(&b_dst->lock);
    }

    return woken + requeued;
}

/* FUTEX_WAKE_OP: atomically modify *uaddr2, wake val waiters at uaddr, then
 * conditionally wake val2 waiters at uaddr2 based on the old value.
 *
 * The op argument encodes: operation on *uaddr2 and comparison predicate.
 * Used by glibc's pthread_cond_signal; musl does NOT use this, but futex
 * emulation implements it for compatibility with glibc-linked binaries.
 *
 * val3 encodes both the operation and comparison:
 *   bits 28-31: op code (SET=0, ADD=1, OR=2, ANDN=3, XOR=4)
 *   bits 24-27: cmp code (EQ=0, NE=1, LT=2, LE=3, GT=4, GE=5)
 *   bits 12-23: op arg
 *   bits  0-11: cmp arg
 */
static int64_t futex_wake_op(guest_t *g,
                             uint64_t uaddr,
                             uint32_t val,
                             uint64_t uaddr2,
                             uint32_t val2,
                             uint32_t val3)
{
    unsigned idx1 = futex_hash(uaddr);
    unsigned idx2 = futex_hash(uaddr2);
    futex_bucket_t *b1 = &buckets[idx1];
    futex_bucket_t *b2 = &buckets[idx2];

    /* Lock ordering */
    if (idx1 == idx2) {
        pthread_mutex_lock(&b1->lock);
    } else if (idx1 < idx2) {
        pthread_mutex_lock(&b1->lock);
        pthread_mutex_lock(&b2->lock);
    } else {
        pthread_mutex_lock(&b2->lock);
        pthread_mutex_lock(&b1->lock);
    }

    /* Decode operation and comparison from val3.
     * Bits 31-28: operation (bit 31 = OPARG_SHIFT flag, bits 30-28 = op)
     * Bits 27-24: comparison operator
     * Bits 23-12: op_arg (operand for modify, 12-bit signed)
     * Bits 11-0:  cmp_arg (operand for compare, 12-bit signed)
     * Both op_arg and cmp_arg are sign-extended from 12 bits to match
     * the Linux kernel's sign_extend32() in futex_atomic_op_inuser().
     */
    unsigned wake_op = (val3 >> 28) & 0xF;
    unsigned wake_cmp = (val3 >> 24) & 0xF;
    int op_arg_raw = (int) ((val3 >> 12) & 0xFFF);
    int op_arg = (op_arg_raw << 20) >> 20; /* Sign-extend 12->32 */
    int cmp_arg_raw = (int) (val3 & 0xFFF);
    int cmp_arg = (cmp_arg_raw << 20) >> 20; /* Sign-extend 12->32 */

    /* FUTEX_OP_OPARG_SHIFT (bit 3 of wake_op): interpret op_arg as 1<<op_arg */
    int op_shift = (int) (wake_op & 8);
    wake_op &= 7; /* Actual operation is bits 0-2 */
    if (op_shift)
        op_arg = (int) (1U << (op_arg & 0x1F));

    /* Atomically modify *uaddr2 */
    uint32_t *word2 = (uint32_t *) guest_ptr_w(g, uaddr2);
    if (!word2) {
        if (idx1 != idx2)
            pthread_mutex_unlock(&b2->lock);
        pthread_mutex_unlock(&b1->lock);
        return -LINUX_EFAULT;
    }

    /* Atomic read-modify-write on *uaddr2 using CAS loop.
     * Matches Linux kernel's futex_atomic_op_inuser() semantics:
     * the modification must be atomic w.r.t. concurrent guest stores.
     */
    uint32_t old_val, new_val;
    do {
        old_val = __atomic_load_n(word2, __ATOMIC_SEQ_CST);
        switch (wake_op) {
        case 0:
            new_val = op_arg;
            break; /* SET */
        case 1:
            new_val = old_val + op_arg;
            break; /* ADD */
        case 2:
            new_val = old_val | op_arg;
            break; /* OR */
        case 3:
            new_val = old_val & ~op_arg;
            break; /* ANDN */
        case 4:
            new_val = old_val ^ op_arg;
            break; /* XOR */
        default:
            new_val = old_val;
            break;
        }
    } while (!__atomic_compare_exchange_n(word2, &old_val, new_val,
                                          /*weak=*/0, __ATOMIC_SEQ_CST,
                                          __ATOMIC_SEQ_CST));

    /* Wake up to val waiters at uaddr (unlink woken entries) */
    int woken = 0;
    futex_waiter_t **pp1 = &b1->head;
    while (*pp1 && (uint32_t) woken < val) {
        futex_waiter_t *w = *pp1;
        if (w->uaddr == uaddr) {
            *pp1 = w->next;
            __atomic_store_n(&w->woken, 1, __ATOMIC_RELEASE);
            pthread_cond_signal(&w->cond);
            futex_waiter_notify_group(w);
            woken++;
        } else {
            pp1 = &w->next;
        }
    }

    /* Evaluate comparison predicate on old_val */
    int cond_met = 0;
    /* Linux FUTEX_WAKE_OP uses signed comparison semantics */
    int32_t sv = (int32_t) old_val, sa = (int32_t) cmp_arg;
    switch (wake_cmp) {
    case 0:
        cond_met = (sv == sa);
        break; /* EQ */
    case 1:
        cond_met = (sv != sa);
        break; /* NE */
    case 2:
        cond_met = (sv < sa);
        break; /* LT (signed) */
    case 3:
        cond_met = (sv <= sa);
        break; /* LE (signed) */
    case 4:
        cond_met = (sv > sa);
        break; /* GT (signed) */
    case 5:
        cond_met = (sv >= sa);
        break; /* GE (signed) */
    default:
        break;
    }

    /* Conditionally wake up to val2 waiters at uaddr2 (unlink woken) */
    if (cond_met) {
        futex_waiter_t **pp2 = &b2->head;
        int woken2 = 0;
        while (*pp2 && (uint32_t) woken2 < val2) {
            futex_waiter_t *w2 = *pp2;
            if (w2->uaddr == uaddr2) {
                *pp2 = w2->next;
                __atomic_store_n(&w2->woken, 1, __ATOMIC_RELEASE);
                pthread_cond_signal(&w2->cond);
                futex_waiter_notify_group(w2);
                woken2++;
            } else {
                pp2 = &w2->next;
            }
        }
        woken += woken2;
    }

    /* Unlock reverse order */
    if (idx1 == idx2) {
        pthread_mutex_unlock(&b1->lock);
    } else if (idx1 < idx2) {
        pthread_mutex_unlock(&b2->lock);
        pthread_mutex_unlock(&b1->lock);
    } else {
        pthread_mutex_unlock(&b1->lock);
        pthread_mutex_unlock(&b2->lock);
    }

    return woken;
}

/* PI (Priority-Inheritance) futex.
 *
 * PI futexes use the futex word itself as an atomic lock:
 *   bits 0-30 = owner TID (FUTEX_TID_MASK), bit 31 = FUTEX_WAITERS
 *
 * Futex emulation does not implement real priority inheritance (boosting the
 * holder's priority to the highest waiter's), but it implements the locking
 * semantics correctly. Some runtimes use PI futexes for internal locks and only
 * need the mutex behavior, not the RT priority boosting. Waiters block on a
 * per-address condition variable (reusing the same bucket hash table as normal
 * futexes).
 */

/* FUTEX_LOCK_PI: Block until the lock at uaddr can be acquired.
 *
 * The PI futex word stores the owner TID in bits 0-30 and a WAITERS flag in bit
 * 31. The kernel emulation sets FUTEX_WAITERS when a thread blocks, so the
 * current owner knows to call FUTEX_UNLOCK_PI instead of releasing the word
 * with the uncontended userspace CAS(TID->0) path.
 *
 * Flow: try CAS(0->TID). If held by another thread, set WAITERS bit via CAS,
 * then block. On wakeup, retry acquisition.
 */
static int64_t futex_lock_pi(guest_t *g, uint64_t uaddr, uint64_t timeout_gva)
{
    uint32_t *word = (uint32_t *) guest_ptr_w(g, uaddr);
    if (!word)
        return -LINUX_EFAULT;

    uint32_t tid = current_thread ? (uint32_t) current_thread->guest_tid
                                  : (uint32_t) proc_get_pid();

    /* Build deadline (if timeout specified, it's absolute CLOCK_REALTIME) */
    bool has_timeout = (timeout_gva != 0);
    struct timespec deadline;
    if (has_timeout) {
        int rc =
            futex_make_deadline(g, timeout_gva, /*is_absolute=*/1, &deadline);
        if (rc == -1)
            return -LINUX_EFAULT;
        if (rc == -2)
            return -LINUX_EINVAL;
    }

    unsigned idx = futex_hash(uaddr);
    futex_bucket_t *b = &buckets[idx];

    for (;;) {
        /* Fast path: try to CAS 0 -> the current TID (uncontended acquisition)
         */
        uint32_t expected = 0;
        if (__atomic_compare_exchange_n(word, &expected, tid,
                                        /*weak=*/0, __ATOMIC_SEQ_CST,
                                        __ATOMIC_SEQ_CST)) {
            return 0; /* Acquired */
        }

        /* Already own it? Deadlock (Linux returns EDEADLK) */
        if ((expected & FUTEX_TID_MASK) == tid)
            return -LINUX_EDEADLK;

        /* Check if the owner thread has exited without releasing the lock.
         * Linux kernel handles this via PI futex cleanup on thread exit; futex
         * emulation detects it lazily here since guest memory does not track PI
         * ownership per-thread. Clear the futex word and retry acquisition.
         */
        uint32_t owner_tid = expected & FUTEX_TID_MASK;
        if (owner_tid != 0 && !thread_find((int64_t) owner_tid)) {
            __atomic_compare_exchange_n(word, &expected, 0,
                                        /*weak=*/0, __ATOMIC_SEQ_CST,
                                        __ATOMIC_SEQ_CST);
            continue; /* Retry acquisition */
        }

        /* Set the WAITERS bit so the owner takes the kernel-mediated unlock
         * path. Retry the CAS in a loop since the owner may release
         * concurrently.
         */
        for (;;) {
            uint32_t cur = __atomic_load_n(word, __ATOMIC_SEQ_CST);
            if ((cur & FUTEX_TID_MASK) == 0)
                break; /* Owner released; retry outer loop */
            if (cur & FUTEX_WAITERS)
                break; /* Already set by another waiter */
            uint32_t desired = cur | FUTEX_WAITERS;
            if (__atomic_compare_exchange_n(word, &cur, desired,
                                            /*weak=*/0, __ATOMIC_SEQ_CST,
                                            __ATOMIC_SEQ_CST))
                break; /* WAITERS bit set */
        }

        /* Re-check after WAITERS bit: if lock is now free, retry */
        uint32_t cur = __atomic_load_n(word, __ATOMIC_SEQ_CST);
        if ((cur & FUTEX_TID_MASK) == 0)
            continue;

        /* Enqueue and block */
        pthread_mutex_lock(&b->lock);

        /* Double-check under bucket lock: owner may have released and called
         * UNLOCK_PI between the current WAITERS set and lock.
         */
        cur = __atomic_load_n(word, __ATOMIC_SEQ_CST);
        if ((cur & FUTEX_TID_MASK) == 0) {
            pthread_mutex_unlock(&b->lock);
            continue;
        }

        futex_waiter_t waiter = {
            .uaddr = uaddr,
            .bitset = FUTEX_BITSET_MATCH_ANY,
            .woken = 0,
            .next = b->head,
        };
        pthread_cond_init(&waiter.cond, NULL);
        b->head = &waiter;

        bool owner_died = false;
        while (!__atomic_load_n(&waiter.woken, __ATOMIC_ACQUIRE)) {
            if (has_timeout) {
                int rc =
                    pthread_cond_timedwait(&waiter.cond, &b->lock, &deadline);
                if (rc != 0 &&
                    !__atomic_load_n(&waiter.woken, __ATOMIC_ACQUIRE)) {
                    /* Timeout: dequeue and return */
                    bucket_unlink_locked(b, &waiter);
                    /* Only clear WAITERS bit if no waiters for this address */
                    bool has_waiters = false;
                    for (futex_waiter_t *w = b->head; w; w = w->next) {
                        if (w->uaddr == uaddr) {
                            has_waiters = true;
                            break;
                        }
                    }
                    pthread_mutex_unlock(&b->lock);
                    pthread_cond_destroy(&waiter.cond);
                    if (!has_waiters) {
                        for (;;) {
                            uint32_t v =
                                __atomic_load_n(word, __ATOMIC_SEQ_CST);
                            if (!(v & FUTEX_WAITERS))
                                break;
                            uint32_t nv = v & ~FUTEX_WAITERS;
                            if (__atomic_compare_exchange_n(word, &v, nv,
                                                            /*weak=*/0,
                                                            __ATOMIC_SEQ_CST,
                                                            __ATOMIC_SEQ_CST))
                                break;
                        }
                    }
                    return -LINUX_ETIMEDOUT;
                }
            } else {
                /* No timeout: poll every 100ms to check exit_group
                 * and dead lock owners.
                 */
                struct timespec poll_ts;
                timespec_deadline_in_ms(&poll_ts, 100);
                pthread_cond_timedwait(&waiter.cond, &b->lock, &poll_ts);

                if (proc_exit_group_requested()) {
                    /* Dequeue and return */
                    bucket_unlink_locked(b, &waiter);
                    pthread_mutex_unlock(&b->lock);
                    pthread_cond_destroy(&waiter.cond);
                    return -LINUX_EINTR;
                }

                /* Check if the owner thread has died while the waiter was
                 * waiting. If so, clear the lock and retry.
                 * Use thread_tid_alive (lock-free) instead of thread_find to
                 * avoid lock order inversion: bucket lock(7) is held here, and
                 * thread_find acquires thread_lock(5).
                 */
                uint32_t check = __atomic_load_n(word, __ATOMIC_SEQ_CST);
                uint32_t check_tid = check & FUTEX_TID_MASK;
                if (check_tid != 0 && !thread_tid_alive((int64_t) check_tid)) {
                    owner_died = true;
                    break;
                }
            }
        }

        /* Dequeue waiter from bucket list */
        futex_waiter_t **pp = &b->head;
        while (*pp) {
            if (*pp == &waiter) {
                *pp = waiter.next;
                break;
            }
            pp = &(*pp)->next;
        }
        pthread_mutex_unlock(&b->lock);
        pthread_cond_destroy(&waiter.cond);

        if (owner_died) {
            /* Clear the dead owner's lock word and retry acquisition */
            uint32_t v = __atomic_load_n(word, __ATOMIC_SEQ_CST);
            __atomic_compare_exchange_n(word, &v, 0,
                                        /*weak=*/0, __ATOMIC_SEQ_CST,
                                        __ATOMIC_SEQ_CST);
            continue;
        }

        /* Woken: retry acquisition. The outer loop re-reads the lock word and
         * retries CAS(0->TID). If FUTEX_WAITERS (bit 31) is still set by other
         * waiters, CAS(0->TID) will fail since the word is non-zero; the loop
         * will then see the WAITERS bit and handle it appropriately.
         */
    }
}

/* FUTEX_TRYLOCK_PI: Non-blocking version of LOCK_PI.
 * CAS 0 -> TID; if the lock is held, return -EAGAIN immediately.
 */
static int64_t futex_trylock_pi(guest_t *g, uint64_t uaddr)
{
    uint32_t *word = (uint32_t *) guest_ptr_w(g, uaddr);
    if (!word)
        return -LINUX_EFAULT;

    uint32_t tid = current_thread ? (uint32_t) current_thread->guest_tid
                                  : (uint32_t) proc_get_pid();

    uint32_t expected = 0;
    if (__atomic_compare_exchange_n(word, &expected, tid,
                                    /*weak=*/0, __ATOMIC_SEQ_CST,
                                    __ATOMIC_SEQ_CST)) {
        return 0; /* Acquired */
    }

    return -LINUX_EAGAIN; /* Lock held, cannot acquire */
}

/* FUTEX_UNLOCK_PI: Release the PI lock at uaddr and wake one waiter.
 *
 * Called by the lock owner when FUTEX_WAITERS is set (slow unlock path).
 * Atomically clear the word to 0 (releasing the lock + clearing WAITERS),
 * then wake one blocked waiter so it can retry CAS(0->TID) acquisition.
 */
static int64_t futex_unlock_pi(guest_t *g, uint64_t uaddr)
{
    uint32_t *word = (uint32_t *) guest_ptr_w(g, uaddr);
    if (!word)
        return -LINUX_EFAULT;

    uint32_t tid = current_thread ? (uint32_t) current_thread->guest_tid
                                  : (uint32_t) proc_get_pid();

    /* Verify the current thread owns the lock (TID field matches) */
    uint32_t cur = __atomic_load_n(word, __ATOMIC_SEQ_CST);
    if ((cur & FUTEX_TID_MASK) != tid)
        return -LINUX_EPERM;

    /* Atomically release: set word to 0 (clear TID + WAITERS flag).
     * Use CAS loop in case another thread is concurrently setting WAITERS.
     */
    for (;;) {
        uint32_t v = __atomic_load_n(word, __ATOMIC_SEQ_CST);
        if (__atomic_compare_exchange_n(word, &v, 0,
                                        /*weak=*/0, __ATOMIC_SEQ_CST,
                                        __ATOMIC_SEQ_CST))
            break;
    }

    /* Wake one waiter so it can retry acquisition */
    unsigned idx = futex_hash(uaddr);
    futex_bucket_t *b = &buckets[idx];

    pthread_mutex_lock(&b->lock);
    futex_waiter_t **pp = &b->head;
    while (*pp) {
        futex_waiter_t *w = *pp;
        if (w->uaddr == uaddr) {
            *pp = w->next; /* Unlink before signaling */
            __atomic_store_n(&w->woken, 1, __ATOMIC_RELEASE);
            pthread_cond_signal(&w->cond);
            futex_waiter_notify_group(w);
            break; /* Wake exactly one */
        }
        pp = &w->next;
    }
    pthread_mutex_unlock(&b->lock);

    return 0;
}

/* Syscall entry point. */

int64_t sys_futex(guest_t *g,
                  uint64_t uaddr,
                  int op,
                  uint32_t val,
                  uint64_t timeout_gva,
                  uint64_t uaddr2,
                  uint32_t val3)
{
    int cmd = op & FUTEX_CMD_MASK;

    switch (cmd) {
    case FUTEX_WAIT:
        return futex_wait(g, uaddr, val, timeout_gva, FUTEX_BITSET_MATCH_ANY,
                          /*is_absolute=*/0);

    case FUTEX_WAKE:
        return futex_wake(uaddr, val, FUTEX_BITSET_MATCH_ANY);

    case FUTEX_REQUEUE:
        /* For REQUEUE, the timeout arg is repurposed as val2 (requeue count) */
        return futex_requeue(g, uaddr, val, (uint32_t) timeout_gva, uaddr2,
                             /*do_cmp=*/0, 0);

    case FUTEX_CMP_REQUEUE:
        /* Same repurposing of timeout -> val2, plus compare against val3 */
        return futex_requeue(g, uaddr, val, (uint32_t) timeout_gva, uaddr2,
                             /*do_cmp=*/1, val3);

    case FUTEX_WAKE_OP:
        /* timeout arg repurposed as val2 (wake count for uaddr2) */
        return futex_wake_op(g, uaddr, val, uaddr2, (uint32_t) timeout_gva,
                             val3);

    case FUTEX_WAIT_BITSET:
        return futex_wait(g, uaddr, val, timeout_gva, val3, /*is_absolute=*/1);

    case FUTEX_WAKE_BITSET:
        return futex_wake(uaddr, val, val3);

    case FUTEX_LOCK_PI:
        return futex_lock_pi(g, uaddr, timeout_gva);

    case FUTEX_UNLOCK_PI:
        return futex_unlock_pi(g, uaddr);

    case FUTEX_TRYLOCK_PI:
        return futex_trylock_pi(g, uaddr);

    default:
        /* Unimplemented futex operation (robust futexes, PI requeue).
         * Return ENOSYS so musl knows to fall back.
         */
        return -LINUX_ENOSYS;
    }
}

int futex_wake_one(guest_t *g, uint64_t uaddr)
{
    (void) g;
    return (int) futex_wake(uaddr, 1, FUTEX_BITSET_MATCH_ANY);
}

/* Unlink a waiter from whichever bucket it currently sits in, with retry on
 * concurrent requeue. The waiter's struct lives on the calling thread's stack;
 * leaving a dangling reference behind is a real host-safety bug because a
 * later wake at the new uaddr would dereference it. The regular futex_wait
 * self-dequeue path handles the same race the same way.
 *
 * Termination: on each iteration we either find w in the bucket (unlink and
 * return), or observe w->woken==1 under the bucket lock (the wake path
 * unlinks before storing woken with RELEASE under the bucket lock; once we
 * acquire that bucket lock we synchronize with it), or determine w was
 * requeued elsewhere (re-hash and retry). Forward progress is guaranteed
 * because every requeue and every wake also holds bucket locks, so once we
 * take the lock for the bucket that hashes w's current uaddr, no concurrent
 * mover can step around us.
 */
static void waitv_unlink(futex_waiter_t *w)
{
    if (__atomic_load_n(&w->woken, __ATOMIC_ACQUIRE))
        return;
    for (;;) {
        unsigned idx = futex_hash(w->uaddr);
        futex_bucket_t *b = &buckets[idx];
        pthread_mutex_lock(&b->lock);
        bool found = false;
        for (futex_waiter_t **pp = &b->head; *pp; pp = &(*pp)->next) {
            if (*pp == w) {
                *pp = w->next;
                found = true;
                break;
            }
        }
        bool was_woken = __atomic_load_n(&w->woken, __ATOMIC_ACQUIRE);
        pthread_mutex_unlock(&b->lock);
        if (found || was_woken)
            return;
        /* w must have been requeued to another bucket while we hashed.
         * Re-read uaddr and try again.
         */
    }
}

/* futex_waitv (SYS 449): batch futex wait on multiple addresses.
 *
 * Blocks until any one of the specified futexes is woken, or a timeout expires.
 * Returns the 0-based index of the woken futex, or negative errno.
 *
 * Linux struct futex_waitv layout (24 bytes per element):
 *   uint64_t val, uaddr, uint32_t flags, __reserved
 */
#define FUTEX_WAITV_MAX 128 /* Linux limit */

#define FUTEX2_SIZE_U32 0x02
#define FUTEX2_SIZE_MASK 0x03
#define FUTEX2_PRIVATE 0x80
#define FUTEX2_VALID_FLAGS (FUTEX2_SIZE_MASK | FUTEX2_PRIVATE)

typedef struct {
    uint64_t val, uaddr;
    uint32_t flags, __reserved;
} linux_futex_waitv_t;

_Static_assert(sizeof(linux_futex_waitv_t) == 24,
               "futex_waitv element must be 24 bytes");

/* Shared wakeup state for futex_waitv. Each enqueued waiter holds pointers to
 * this struct so any wake site (futex_wake, futex_requeue, futex_wake_op,
 * futex_unlock_pi) signals shared.cond after marking the waiter woken. The
 * polling loop sleeps on shared.cond with a bounded timeout so it still picks
 * up exit_group requests and real timeouts even when no signal arrives.
 */
typedef struct {
    pthread_mutex_t lock;
    pthread_cond_t cond;
} waitv_shared_t;

/* Linux clockid values accepted by futex_waitv. */
#define LINUX_CLOCK_REALTIME 0
#define LINUX_CLOCK_MONOTONIC 1

static int waitv_collect_buckets(const linux_futex_waitv_t *elts,
                                 uint32_t nr_futexes,
                                 unsigned bucket_ids[FUTEX_WAITV_MAX],
                                 futex_bucket_t *bucket_ptrs[FUTEX_WAITV_MAX])
{
    unsigned nbuckets = 0;

    for (uint32_t i = 0; i < nr_futexes; i++) {
        unsigned idx = futex_hash(elts[i].uaddr);
        unsigned pos = 0;

        while (pos < nbuckets && bucket_ids[pos] < idx)
            pos++;
        if (pos < nbuckets && bucket_ids[pos] == idx)
            continue;

        for (unsigned j = nbuckets; j > pos; j--) {
            bucket_ids[j] = bucket_ids[j - 1];
            bucket_ptrs[j] = bucket_ptrs[j - 1];
        }
        bucket_ids[pos] = idx;
        bucket_ptrs[pos] = &buckets[idx];
        nbuckets++;
    }

    return (int) nbuckets;
}

int64_t sys_futex_waitv(guest_t *g,
                        uint64_t waiters_gva,
                        uint32_t nr_futexes,
                        uint32_t flags,
                        uint64_t timeout_gva,
                        int clockid)
{
    /* Validation order matches Linux do_futex_waitv():
     *   1. flags
     *   2. nr_futexes / !waiters
     *   3. clockid (when timeout != NULL)
     *   4. copy_from_user(timeout) -> EFAULT
     *   5. timespec64_valid(timeout) -> EINVAL
     *   6. copy_from_user(waiters) -> EFAULT
     *   7. per-element validate -> EINVAL
     * Reordering steps 4-7 to match Linux means a guest that passes a bad
     * timeout AND bad waiters sees the same errno Linux would, instead of
     * having ours fault on waiters first.
     */
    if (flags != 0)
        return -LINUX_EINVAL;
    if (nr_futexes == 0 || nr_futexes > FUTEX_WAITV_MAX || waiters_gva == 0)
        return -LINUX_EINVAL;

    bool has_timeout = (timeout_gva != 0);
    if (has_timeout && clockid != LINUX_CLOCK_REALTIME &&
        clockid != LINUX_CLOCK_MONOTONIC)
        return -LINUX_EINVAL;

    /* Copy and validate the timeout before reading the waiters array. */
    struct timespec deadline;
    if (has_timeout) {
        linux_timespec_t lts;
        if (guest_read_small(g, timeout_gva, &lts, sizeof(lts)) < 0)
            return -LINUX_EFAULT;
        if (!linux_timespec_is_valid(&lts))
            return -LINUX_EINVAL;

        if (clockid == LINUX_CLOCK_MONOTONIC) {
            /* Translate the monotonic absolute deadline to a CLOCK_REALTIME
             * absolute deadline so pthread_cond_timedwait (which uses
             * CLOCK_REALTIME) waits the right amount. macOS has no
             * CLOCK_MONOTONIC condattr, so this conversion is unavoidable;
             * minor wall-clock skew is accepted. lts.tv_sec is bounded by
             * FUTEX_TIMESPEC_SEC_MAX (linux_timespec_is_valid), so the
             * subtraction and addition stay inside int64_t / time_t range.
             */
            struct timeval now;
            gettimeofday(&now, NULL);
            struct timespec mono;
            clock_gettime(CLOCK_MONOTONIC, &mono);
            int64_t delta_sec = lts.tv_sec - mono.tv_sec;
            long delta_nsec = (long) lts.tv_nsec - mono.tv_nsec;
            deadline.tv_sec = now.tv_sec + delta_sec;
            deadline.tv_nsec = (long) now.tv_usec * 1000 + delta_nsec;
        } else {
            deadline.tv_sec = (time_t) lts.tv_sec;
            deadline.tv_nsec = (long) lts.tv_nsec;
        }
        timespec_normalize(&deadline);
    }

    linux_futex_waitv_t elts[FUTEX_WAITV_MAX];
    size_t sz = nr_futexes * sizeof(linux_futex_waitv_t);
    if (guest_read_small(g, waiters_gva, elts, sz) < 0)
        return -LINUX_EFAULT;

    for (uint32_t i = 0; i < nr_futexes; i++) {
        if (elts[i].__reserved != 0)
            return -LINUX_EINVAL;
        if (elts[i].flags & ~FUTEX2_VALID_FLAGS)
            return -LINUX_EINVAL;
        if ((elts[i].flags & FUTEX2_SIZE_MASK) != FUTEX2_SIZE_U32)
            return -LINUX_EINVAL;
        /* uaddr must be naturally aligned for the declared size. For
         * FUTEX2_SIZE_U32 that is 4-byte alignment; an unaligned futex word
         * loses atomicity on aarch64 and matches no kernel-side behavior.
         */
        if (elts[i].uaddr & 0x3)
            return -LINUX_EINVAL;
    }

    waitv_shared_t shared;
    pthread_mutex_init(&shared.lock, NULL);
    pthread_cond_init(&shared.cond, NULL);

    /* Validate and enqueue while holding every distinct bucket lock in index
     * order so the whole wait set is checked atomically.
     */
    futex_waiter_t waiters[FUTEX_WAITV_MAX];
    unsigned bucket_ids[FUTEX_WAITV_MAX];
    futex_bucket_t *bucket_ptrs[FUTEX_WAITV_MAX];
    int nbuckets =
        waitv_collect_buckets(elts, nr_futexes, bucket_ids, bucket_ptrs);
    int enqueued = 0;
    int64_t result_err = 0;

    for (int i = 0; i < nbuckets; i++)
        pthread_mutex_lock(&bucket_ptrs[i]->lock);

    for (uint32_t i = 0; i < nr_futexes; i++) {
        uint64_t uaddr = elts[i].uaddr;
        uint32_t expected = (uint32_t) elts[i].val;
        unsigned idx = futex_hash(uaddr);
        futex_bucket_t *b = &buckets[idx];

        uint32_t *word = (uint32_t *) guest_ptr(g, uaddr);
        if (!word) {
            result_err = -LINUX_EFAULT;
            goto unlock_early;
        }

        uint32_t current = __atomic_load_n(word, __ATOMIC_SEQ_CST);
        if (current != expected) {
            result_err = -LINUX_EAGAIN;
            goto unlock_early;
        }

        futex_waiter_t *w = &waiters[i];
        w->uaddr = uaddr;
        w->bitset = FUTEX_BITSET_MATCH_ANY;
        w->woken = 0;
        w->next = b->head;
        w->group_lock = &shared.lock;
        w->group_cond = &shared.cond;
        pthread_cond_init(&w->cond, NULL);
        b->head = w;
        enqueued++;
    }

    for (int i = nbuckets - 1; i >= 0; i--)
        pthread_mutex_unlock(&bucket_ptrs[i]->lock);

    /* All enqueued. Block on shared.cond until any wake site signals it.
     * The bounded sleep (capped at 500ms or the user deadline, whichever is
     * sooner) gives proc_exit_group_requested() and timeout checks a chance to
     * run if the cond_signal never arrives.
     */
    int result_idx = -1;
    pthread_mutex_lock(&shared.lock);
    for (;;) {
        for (uint32_t i = 0; i < nr_futexes; i++) {
            if (__atomic_load_n(&waiters[i].woken, __ATOMIC_ACQUIRE)) {
                result_idx = (int) i;
                break;
            }
        }
        if (result_idx >= 0)
            break;

        if (proc_exit_group_requested()) {
            result_idx = -LINUX_EINTR;
            break;
        }

        struct timespec wait_ts;
        timespec_deadline_in_ms(&wait_ts, 500);
        if (has_timeout) {
            if (deadline.tv_sec < wait_ts.tv_sec ||
                (deadline.tv_sec == wait_ts.tv_sec &&
                 deadline.tv_nsec < wait_ts.tv_nsec)) {
                wait_ts = deadline;
            }
        }

        pthread_cond_timedwait(&shared.cond, &shared.lock, &wait_ts);

        if (has_timeout) {
            struct timeval now;
            gettimeofday(&now, NULL);
            long now_ns = (long) now.tv_usec * 1000;
            bool past_deadline =
                now.tv_sec > deadline.tv_sec ||
                (now.tv_sec == deadline.tv_sec && now_ns >= deadline.tv_nsec);
            if (past_deadline) {
                /* Re-check woken under shared.lock before declaring a timeout:
                 * a wake that arrived during the cond_timedwait may not have
                 * been signalled yet on this thread but the woken flag is set.
                 */
                for (uint32_t i = 0; i < nr_futexes; i++) {
                    if (__atomic_load_n(&waiters[i].woken, __ATOMIC_ACQUIRE)) {
                        result_idx = (int) i;
                        break;
                    }
                }
                if (result_idx < 0)
                    result_idx = -LINUX_ETIMEDOUT;
                break;
            }
        }
    }
    pthread_mutex_unlock(&shared.lock);

    /* Unlink all waiters (woken entries are already removed by the wake path,
     * but a second pass is harmless and avoids stale pointers).
     */
    for (uint32_t i = 0; i < nr_futexes; i++)
        waitv_unlink(&waiters[i]);

    for (uint32_t i = 0; i < nr_futexes; i++)
        pthread_cond_destroy(&waiters[i].cond);
    pthread_mutex_destroy(&shared.lock);
    pthread_cond_destroy(&shared.cond);

    return result_idx;

unlock_early:
    for (int i = nbuckets - 1; i >= 0; i--)
        pthread_mutex_unlock(&bucket_ptrs[i]->lock);

    for (int i = enqueued - 1; i >= 0; i--) {
        waitv_unlink(&waiters[i]);
        pthread_cond_destroy(&waiters[i].cond);
    }
    pthread_mutex_destroy(&shared.lock);
    pthread_cond_destroy(&shared.cond);
    return result_err;
}

/* Robust futex list walk. */

/* Linux robust_list_head layout:
 *   struct robust_list_head {
 *       struct robust_list *list;       offset 0: pointer to first entry
 *       long futex_offset;              offset 8: offset from entry to futex
 *       word struct robust_list *list_op_pending; offset 16: in-progress lock
 *   };
 *
 * Each entry in the list:
 *   struct robust_list {
 *       struct robust_list *next;       pointer to next (or back to head)
 *   };
 *
 * The futex word is at (entry_addr + futex_offset). The list is circular:
 * list->next... eventually points back to &head->list.
 */

#define ROBUST_LIST_LIMIT 2048 /* safety bound against corrupted lists */

void robust_list_walk(guest_t *g, thread_entry_t *t)
{
    uint64_t head_gva = t->robust_list_head;
    if (head_gva == 0)
        return;

    /* Read robust_list_head: { list, futex_offset, list_op_pending } */
    uint64_t head[3];
    if (guest_read_small(g, head_gva, head, sizeof(head)) < 0)
        return;

    uint64_t list_ptr = head[0]; /* pointer to first robust_list entry */
    int64_t futex_offset = (int64_t) head[1];
    uint64_t pending = head[2]; /* list_op_pending */

    /* The head of the list is at &head->list (which is head_gva itself).
     * Walk entries until the walk loops back to the head pointer.
     */
    uint64_t head_entry = head_gva; /* address of head->list field */
    int count = 0;

    while (list_ptr != head_entry && count < ROBUST_LIST_LIMIT) {
        /* The futex word is at list_ptr + futex_offset.
         * Use unsigned add to avoid signed overflow UB; skip entries where the
         * result wraps past the guest address space.
         */
        uint64_t futex_gva;
        if (futex_offset >= 0)
            futex_gva = list_ptr + (uint64_t) futex_offset;
        else
            futex_gva = list_ptr - (uint64_t) (-futex_offset);
        if (futex_gva >= g->ipa_base + g->guest_size) {
            /* Address out of guest range; skip this entry */
            uint64_t next;
            if (guest_read_small(g, list_ptr, &next, sizeof(next)) < 0)
                break;
            list_ptr = next;
            count++;
            continue;
        }

        /* Read the futex word */
        uint32_t futex_val;
        if (guest_read_small(g, futex_gva, &futex_val, sizeof(futex_val)) ==
            0) {
            /* Only act if this thread owns the lock */
            uint32_t owner = futex_val & FUTEX_TID_MASK;
            if (owner == (uint32_t) t->guest_tid) {
                /* Set FUTEX_OWNER_DIED and clear TID */
                uint32_t new_val =
                    (futex_val & ~FUTEX_TID_MASK) | FUTEX_OWNER_DIED;
                guest_write_small(g, futex_gva, &new_val, sizeof(new_val));
                futex_wake(futex_gva, 1, FUTEX_BITSET_MATCH_ANY);
            }
        }

        /* Read next pointer */
        uint64_t next;
        if (guest_read_small(g, list_ptr, &next, sizeof(next)) < 0)
            break;
        list_ptr = next;
        count++;
    }

    /* Handle pending operation (lock that was being acquired when the thread
     * died)
     */
    if (pending && pending != head_entry) {
        uint64_t futex_gva;
        if (futex_offset >= 0)
            futex_gva = pending + (uint64_t) futex_offset;
        else
            futex_gva = pending - (uint64_t) (-futex_offset);
        uint32_t futex_val;
        if (guest_read_small(g, futex_gva, &futex_val, sizeof(futex_val)) ==
            0) {
            uint32_t owner = futex_val & FUTEX_TID_MASK;
            if (owner == (uint32_t) t->guest_tid) {
                uint32_t new_val =
                    (futex_val & ~FUTEX_TID_MASK) | FUTEX_OWNER_DIED;
                guest_write_small(g, futex_gva, &new_val, sizeof(new_val));
                futex_wake(futex_gva, 1, FUTEX_BITSET_MATCH_ANY);
            }
        }
    }
}
