/* Per-thread state for Linux threading support
 *
 * Copyright 2026 elfuse contributors
 * Copyright 2025 Moritz Angermann, zw3rk pte. ltd.
 * SPDX-License-Identifier: Apache-2.0
 *
 * Thread table with _Thread_local fast path for current thread access.
 * Protected by a mutex since thread creation/destruction is infrequent
 * relative to per-syscall access (which uses the lock-free TLS pointer).
 */

#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <stdatomic.h>
#include <time.h>
#include <unistd.h>

#include "utils.h"

#include "runtime/thread.h"
#include "debug/log.h"
#include "core/guest.h" /* SHIM_DATA_BASE, BLOCK_2MIB, GUEST_IPA_BASE */
#include "hvutil.h"     /* vcpu_get_gpr, vcpu_get_sysreg */

/* From syscall/signal.h, included here directly to avoid pulling in
 * the full signal header (macOS defines sa_handler as a macro that
 * conflicts with the linux_sigaction_t field name).
 */
#define LINUX_SS_DISABLE 2

static void thread_ptrace_init(thread_entry_t *t);

/* Top of the EL1 exception stack region (one 4KiB slot per thread) */
#define SP_EL1_TOP (GUEST_IPA_BASE + SHIM_DATA_BASE + BLOCK_2MIB)

/* Thread table. */

static thread_entry_t thread_table[MAX_THREADS];
static pthread_mutex_t thread_lock =
    PTHREAD_MUTEX_INITIALIZER; /* Lock order: 5 */
static _Atomic int active_thread_count = 0;

/* Iterate every slot. */
#define THREAD_FOR_EACH(t) \
    for (thread_entry_t *t = thread_table; t < thread_table + MAX_THREADS; t++)

/* Iterate active slots. Caller must hold thread_lock for stable reads. */
#define THREAD_FOR_EACH_ACTIVE(t) \
    THREAD_FOR_EACH (t)           \
        if (t->active)

/* Iterate active slots without holding thread_lock. Uses an acquire load on
 * the active flag so the lock-free observers in thread_tid_alive() and
 * thread_signal_deliverable() see a consistent transition.
 */
#define THREAD_FOR_EACH_ACTIVE_RELAXED(t) \
    THREAD_FOR_EACH (t)                   \
        if (__atomic_load_n(&t->active, __ATOMIC_ACQUIRE))

/* Bitmask tracking allocated SP_EL1 slots. Bit N set = slot N in use.
 * MAX_THREADS=64 fits exactly in a uint64_t. Slot 0 is the main thread (top of
 * shim data region); each subsequent slot is 4KiB below.
 */
static uint64_t sp_el1_allocated = 0;

/* Per-thread pointer to the current thread's entry. Set once when a host
 * pthread starts running a guest vCPU. Syscall handlers read this without
 * locking since it is thread-local and never changes after init.
 */
_Thread_local thread_entry_t *current_thread = NULL;

/* Public API */

void thread_init(void)
{
    pthread_mutex_lock(&thread_lock);
    memset(thread_table, 0, sizeof(thread_table));
    sp_el1_allocated = 0;
    atomic_store(&active_thread_count, 0);
    current_thread = NULL;
    pthread_mutex_unlock(&thread_lock);
}

void thread_register_main(hv_vcpu_t vcpu,
                          hv_vcpu_exit_t *vexit,
                          int64_t tid,
                          uint64_t sp_el1)
{
    pthread_mutex_lock(&thread_lock);

    thread_entry_t *t = &thread_table[0];
    t->guest_tid = tid;
    t->vcpu = vcpu;
    t->vexit = vexit;
    t->host_thread = pthread_self();
    t->clear_child_tid = 0;
    t->sp_el1 = sp_el1;
    t->active = 1;
    t->altstack_flags = LINUX_SS_DISABLE;
    t->on_altstack = false;
    thread_ptrace_init(t);

    /* Slot 0 is consumed by main thread */
    sp_el1_allocated = BIT64(0);
    atomic_store(&active_thread_count, 1);

    pthread_mutex_unlock(&thread_lock);

    /* Set TLS pointer for the main thread */
    current_thread = t;
}

thread_entry_t *thread_alloc(int64_t tid)
{
    thread_entry_t *result = NULL;

    pthread_mutex_lock(&thread_lock);
    THREAD_FOR_EACH (t) {
        if (t->active)
            continue;
        /* Skip slots where a tracer is still inside pthread_cond_wait on
         * ptrace_cond. Memset+reinit while a waiter holds a reference is UB.
         */
        if (t->ptrace_conds_inited && t->ptrace_waiters > 0)
            continue;
        if (t->ptrace_conds_inited) {
            pthread_cond_destroy(&t->ptrace_cond);
            pthread_cond_destroy(&t->resume_cond);
        }
        memset(t, 0, sizeof(*t));
        t->guest_tid = tid;
        t->active = 1;
        t->altstack_flags = LINUX_SS_DISABLE;
        thread_ptrace_init(t);
        atomic_fetch_add(&active_thread_count, 1);
        result = t;
        break;
    }
    pthread_mutex_unlock(&thread_lock);

    return result;
}

/* Free an SP_EL1 slot for reuse. Must be called with thread_lock held.
 * Derives the slot index from the IPA and clears the bitmask bit.
 */
static void thread_free_sp_el1_locked(uint64_t sp)
{
    if (sp == 0)
        return;
    uint64_t top = SP_EL1_TOP;
    if (sp > top)
        return;
    int slot = (int) ((top - sp) / 4096);
    if (RANGE_CHECK(slot, 0, MAX_THREADS))
        sp_el1_allocated &= ~BIT64(slot);
}

static void thread_ptrace_cleanup_locked(thread_entry_t *t)
{
    if (!t->ptrace_conds_inited || t->active || t->ptrace_waiters != 0)
        return;

    pthread_cond_destroy(&t->ptrace_cond);
    pthread_cond_destroy(&t->resume_cond);
    t->ptrace_conds_inited = false;
    t->ptrace_cleanup_pending = false;
}

void thread_deactivate(thread_entry_t *t)
{
    if (!t)
        return;

    pthread_mutex_lock(&thread_lock);

    /* If this is a VM-clone child, mark it as exited and wake the
     * tracer/parent so wait4 can collect the exit status.  Must happen BEFORE
     * destroying the condvars, since broadcasting a destroyed condvar is
     * undefined behavior.
     */
    /* Guard against double-deactivation: if already inactive, skip. */
    if (!t->active) {
        pthread_mutex_unlock(&thread_lock);
        return;
    }

    if (t->is_vm_clone) {
        t->vm_exited = true;
        pthread_cond_broadcast(&t->ptrace_cond);
    }

    /* Free SP_EL1 slot so it can be reused by future threads */
    thread_free_sp_el1_locked(t->sp_el1);

    t->active = 0;
    atomic_fetch_sub(&active_thread_count, 1);

    /* Destroy condvars once no waiters still reference them. A tracer woken by
     * the broadcast above may still be re-acquiring thread_lock.
     */
    t->ptrace_cleanup_pending = true;
    thread_ptrace_cleanup_locked(t);

    pthread_mutex_unlock(&thread_lock);
}

thread_entry_t *thread_find(int64_t tid)
{
    thread_entry_t *result = NULL;

    pthread_mutex_lock(&thread_lock);
    THREAD_FOR_EACH_ACTIVE (t) {
        if (t->guest_tid == tid) {
            result = t;
            break;
        }
    }
    pthread_mutex_unlock(&thread_lock);

    return result;
}

int thread_tid_alive(int64_t tid)
{
    /* Lock-free scan: active transitions 1->0 exactly once (in
     * thread_deactivate under thread_lock), and guest_tid is set at allocation
     * and never changes until the slot is reused (after active=0). A stale read
     * of active=1 for a thread being deactivated is benign; the caller retries
     * on the next poll iteration (100ms later).
     */
    THREAD_FOR_EACH_ACTIVE_RELAXED (t)
        if (t->guest_tid == tid)
            return 1;
    return 0;
}

int thread_active_count(void)
{
    return atomic_load(&active_thread_count);
}

int thread_is_single_active(void)
{
    return atomic_load(&active_thread_count) == 1;
}

int thread_count_active_vm_clones(void)
{
    int count = 0;

    pthread_mutex_lock(&thread_lock);
    THREAD_FOR_EACH_ACTIVE (t)
        if (t->is_vm_clone && !t->vm_exited)
            count++;
    pthread_mutex_unlock(&thread_lock);

    return count;
}

uint64_t thread_alloc_sp_el1(void)
{
    uint64_t sp = 0;

    pthread_mutex_lock(&thread_lock);

    /* Find the first free slot (lowest clear bit in the bitmask). */
    uint64_t free_mask = ~sp_el1_allocated & bit_mask64_low(MAX_THREADS);
    if (free_mask == 0) {
        log_error("thread: SP_EL1 slots exhausted");
    } else {
        int slot = bit_ctz64(free_mask);
        /* Main thread's SP_EL1 = IPA_BASE + SHIM_DATA_BASE + 2MiB.
         * Each subsequent thread is 4KiB below.
         */
        uint64_t top = SP_EL1_TOP;
        sp = top - (uint64_t) slot * 4096;
        sp_el1_allocated |= BIT64(slot);
    }

    pthread_mutex_unlock(&thread_lock);

    return sp;
}

void thread_for_each(void (*fn)(thread_entry_t *t, void *ctx), void *ctx)
{
    pthread_mutex_lock(&thread_lock);
    THREAD_FOR_EACH_ACTIVE (t)
        fn(t, ctx);
    pthread_mutex_unlock(&thread_lock);
}

void thread_join_workers(void)
{
    /* Snapshot worker threads under the lock. The code needs the host_thread
     * handle and a way to check the active flag without re-locking. Storing the
     * table entry pointer lets the loop use __atomic_load on active.
     */
    struct {
        pthread_t thr;
        thread_entry_t *t;
    } workers[MAX_THREADS];
    int nworkers = 0;

    pthread_mutex_lock(&thread_lock);
    THREAD_FOR_EACH_ACTIVE (t) {
        if (t == current_thread)
            continue;
        workers[nworkers].thr = t->host_thread;
        workers[nworkers].t = t;
        nworkers++;
    }
    pthread_mutex_unlock(&thread_lock);

    /* Poll/join each worker OUTSIDE the lock. Workers that responded to
     * hv_vcpus_exit typically finish within microseconds. Threads stuck in
     * uninterruptible host calls (blocking read/poll) are given 100ms to
     * finish. If still alive, detach and let process exit clean up. The vCPU
     * is NOT destroyed here because HVF vCPUs are thread-affine, so
     * cross-thread hv_vcpu_destroy while the owning thread may still be inside
     * hv_vcpu_run is unsafe.
     */
    for (int w = 0; w < nworkers; w++) {
        for (int i = 0; i < 20; i++) {
            if (!__atomic_load_n(&workers[w].t->active, __ATOMIC_ACQUIRE))
                break;
            usleep(5000);
        }

        if (!__atomic_load_n(&workers[w].t->active, __ATOMIC_ACQUIRE))
            pthread_join(workers[w].thr, NULL);
        else
            pthread_detach(workers[w].thr);
    }
}

void thread_destroy_all_vcpus(void)
{
    pthread_mutex_lock(&thread_lock);
    THREAD_FOR_EACH_ACTIVE (t) {
        if (!t->vcpu)
            continue;
        hv_vcpu_destroy(t->vcpu);
        t->vcpu = 0;
        thread_free_sp_el1_locked(t->sp_el1);
        t->active = 0;
        /* Do NOT destroy condvars. Same race as thread_deactivate: a waiter
         * woken by an earlier broadcast may still reference the condvar.
         * Process is exiting, so the leak is harmless.
         */
    }
    atomic_store(&active_thread_count, 0);
    pthread_mutex_unlock(&thread_lock);
}

void thread_interrupt_all(void)
{
    /* Collect active vCPUs under the lock, then call hv_vcpus_exit outside
     * the lock to avoid holding it during a framework call.
     */
    hv_vcpu_t vcpus[MAX_THREADS];
    int count = 0;

    pthread_mutex_lock(&thread_lock);
    THREAD_FOR_EACH_ACTIVE (t)
        vcpus[count++] = t->vcpu;
    pthread_mutex_unlock(&thread_lock);

    /* Force all active vCPUs out of hv_vcpu_run(). Each vCPU will see
     * HV_EXIT_REASON_CANCELED and check for pending signals.
     */
    if (count > 0)
        hv_vcpus_exit(vcpus, (uint32_t) count);
}

int thread_signal_deliverable(uint64_t sigbit)
{
    /* Lock-free scan: each thread's blocked field is written under sig_lock
     * (lock order 4), but the code reads it without locking here. A stale read
     * is benign: worst case is a harmless spurious interrupt after reading
     * blocked=0 concurrently with a transition to blocked=1, or a delayed
     * delivery corrected by the next signal_pending() check.
     */
    THREAD_FOR_EACH_ACTIVE_RELAXED (t) {
        uint64_t mask = __atomic_load_n(&t->blocked, __ATOMIC_ACQUIRE);
        if (sigbit & ~mask)
            return 1;
    }
    return 0;
}

/* Fork quiesce. */

/* Fork barrier state. Protected by thread_lock. */
static bool fork_quiesce_active = false; /* True while a fork is in progress */
static int fork_quiesced_count = 0;      /* Siblings blocked on barrier */
static int fork_target_count = 0;        /* Number of siblings to quiesce */
static pthread_cond_t fork_cond = PTHREAD_COND_INITIALIZER;
static pthread_cond_t fork_all_quiesced_cond = PTHREAD_COND_INITIALIZER;

void thread_quiesce_siblings(void)
{
    hv_vcpu_t vcpus[MAX_THREADS];
    int count = 0;

    pthread_mutex_lock(&thread_lock);

    /* Collect sibling vCPUs (all active threads except caller). */
    THREAD_FOR_EACH_ACTIVE (t)
        if (t != current_thread)
            vcpus[count++] = t->vcpu;

    if (count == 0) {
        pthread_mutex_unlock(&thread_lock);
        return;
    }

    /* Arm the barrier */
    fork_quiesce_active = true;
    fork_quiesced_count = 0;
    fork_target_count = count;

    pthread_mutex_unlock(&thread_lock);

    /* Force siblings out of hv_vcpu_run */
    hv_vcpus_exit(vcpus, (uint32_t) count);

    /* Wait until all siblings have blocked on the barrier.
     * Use a bounded wait: siblings in long-running host syscalls (poll, read,
     * accept) may not reach the barrier check promptly since hv_vcpus_exit only
     * affects threads inside hv_vcpu_run. After the timeout, proceed with the
     * snapshot; the sibling is blocked in a host syscall and not mutating guest
     * memory.
     */
    pthread_mutex_lock(&thread_lock);
    if (fork_quiesced_count < fork_target_count) {
        struct timespec deadline;
        timespec_deadline_in_ms(&deadline, 100);
        while (fork_quiesced_count < fork_target_count) {
            int rc = pthread_cond_timedwait(&fork_all_quiesced_cond,
                                            &thread_lock, &deadline);
            if (rc == ETIMEDOUT)
                break; /* Proceed with snapshot anyway */
        }
    }
    pthread_mutex_unlock(&thread_lock);
}

void thread_resume_siblings(void)
{
    pthread_mutex_lock(&thread_lock);
    fork_quiesce_active = false;
    fork_quiesced_count = 0;
    fork_target_count = 0;
    pthread_cond_broadcast(&fork_cond);
    pthread_mutex_unlock(&thread_lock);
}

int thread_fork_barrier_check(void)
{
    pthread_mutex_lock(&thread_lock);
    if (!fork_quiesce_active) {
        pthread_mutex_unlock(&thread_lock);
        return 0;
    }

    /* Signal that the current thread has reached the barrier */
    fork_quiesced_count++;
    if (fork_quiesced_count >= fork_target_count)
        pthread_cond_signal(&fork_all_quiesced_cond);

    /* Block until fork is complete */
    while (fork_quiesce_active)
        pthread_cond_wait(&fork_cond, &thread_lock);

    pthread_mutex_unlock(&thread_lock);
    return 1;
}

/* Ptrace helpers. */

pthread_mutex_t *thread_get_lock(void)
{
    return &thread_lock;
}

static void thread_ptrace_init(thread_entry_t *t)
{
    t->ptraced = false;
    t->tracer_tid = 0;
    t->ptrace_stopped = false;
    t->ptrace_stop_sig = 0;
    t->ptrace_cont_sig = 0;
    t->ptrace_regs_dirty = false;
    t->is_vm_clone = false;
    t->parent_tid = 0;
    t->exit_signal = 0;
    t->vm_exited = false;
    t->vm_exit_status = 0;
    memset(&t->ptrace_regs, 0, sizeof(t->ptrace_regs));
    pthread_cond_init(&t->ptrace_cond, NULL);
    pthread_cond_init(&t->resume_cond, NULL);
    t->ptrace_conds_inited = true;
    t->ptrace_waiters = 0;
    t->ptrace_cleanup_pending = false;
}

int thread_ptrace_stop(thread_entry_t *t, int sig)
{
    pthread_mutex_lock(&thread_lock);

    /* Snapshot vCPU registers into ptrace_regs so the tracer can read them
     * without cross-thread HVF access.
     */
    vcpu_snapshot_gprs(t->vcpu, t->ptrace_regs.regs);
    t->ptrace_regs.sp = vcpu_get_sysreg(t->vcpu, HV_SYS_REG_SP_EL0);
    t->ptrace_regs.pc = vcpu_get_sysreg(t->vcpu, HV_SYS_REG_ELR_EL1);
    t->ptrace_regs.pstate = vcpu_get_sysreg(t->vcpu, HV_SYS_REG_SPSR_EL1);

    /* Enter ptrace-stop state */
    t->ptrace_stopped = true;
    t->ptrace_stop_sig = sig;
    t->ptrace_cont_sig = 0;
    t->ptrace_regs_dirty = false;

    /* Wake the tracer (blocked in thread_ptrace_wait) */
    pthread_cond_broadcast(&t->ptrace_cond);

    /* Block until tracer calls PTRACE_CONT */
    while (t->ptrace_stopped)
        pthread_cond_wait(&t->resume_cond, &thread_lock);

    /* Apply register changes if tracer wrote via SETREGSET */
    if (t->ptrace_regs_dirty) {
        vcpu_restore_gprs(t->vcpu, t->ptrace_regs.regs);
        vcpu_set_sysreg(t->vcpu, HV_SYS_REG_SP_EL0, t->ptrace_regs.sp);
        vcpu_set_sysreg(t->vcpu, HV_SYS_REG_ELR_EL1, t->ptrace_regs.pc);
        vcpu_set_sysreg(t->vcpu, HV_SYS_REG_SPSR_EL1, t->ptrace_regs.pstate);
        t->ptrace_regs_dirty = false;
    }

    int cont_sig = t->ptrace_cont_sig;
    pthread_mutex_unlock(&thread_lock);

    return cont_sig;
}

void thread_ptrace_cont(thread_entry_t *t, int sig)
{
    pthread_mutex_lock(&thread_lock);
    t->ptrace_cont_sig = sig;
    t->ptrace_stopped = false;
    pthread_cond_signal(&t->resume_cond);
    pthread_mutex_unlock(&thread_lock);
}

/* True when t is a waitable child of the given tracer, optionally narrowed by
 * pid (>0 = exact TID, <=0 = any). Caller holds thread_lock.
 */
static bool thread_matches_tracer_child(const thread_entry_t *t,
                                        int64_t tracer_tid,
                                        int pid)
{
    if (!t->active)
        return false;
    bool is_child = (t->ptraced && t->tracer_tid == tracer_tid) ||
                    (t->is_vm_clone && t->parent_tid == tracer_tid);
    if (!is_child)
        return false;
    if (pid > 0 && t->guest_tid != pid)
        return false;
    return true;
}

int64_t thread_ptrace_wait(int64_t tracer_tid,
                           int pid,
                           int *out_status,
                           int options)
{
    int wnohang = (options & 1); /* WNOHANG = 1 on Linux */

    pthread_mutex_lock(&thread_lock);

    for (;;) {
        bool found_any = false; /* Any waitable children at all? */

        THREAD_FOR_EACH (t) {
            if (!thread_matches_tracer_child(t, tracer_tid, pid))
                continue;

            found_any = true;

            /* Ptrace-stopped: report stop signal in wait status. */
            if (t->ptrace_stopped) {
                int64_t tid = t->guest_tid;
                if (out_status)
                    *out_status = (t->ptrace_stop_sig << 8) | 0x7F;
                pthread_mutex_unlock(&thread_lock);
                return tid;
            }

            /* VM-clone child exited: reap and deactivate. */
            if (t->vm_exited) {
                int64_t tid = t->guest_tid;
                if (out_status)
                    *out_status = t->vm_exit_status;
                /* Destroy condvars after the last waiter returns from
                 * pthread_cond_wait().
                 */
                thread_free_sp_el1_locked(t->sp_el1);
                t->active = 0;
                atomic_fetch_sub(&active_thread_count, 1);
                t->ptrace_cleanup_pending = true;
                thread_ptrace_cleanup_locked(t);
                pthread_mutex_unlock(&thread_lock);
                return tid;
            }
        }

        if (!found_any) {
            pthread_mutex_unlock(&thread_lock);
            return 0; /* No matching children; let caller fall through */
        }

        if (wnohang) {
            pthread_mutex_unlock(&thread_lock);
            return 0;
        }

        /* Block on the first matching child's ptrace_cond. In practice VM-clone
         * has one tracee, so this scans little.
         */
        THREAD_FOR_EACH (t) {
            if (!thread_matches_tracer_child(t, tracer_tid, pid))
                continue;

            t->ptrace_waiters++;
            pthread_cond_wait(&t->ptrace_cond, &thread_lock);
            t->ptrace_waiters--;
            if (t->ptrace_cleanup_pending)
                thread_ptrace_cleanup_locked(t);
            break; /* Re-scan after wakeup */
        }
    }
}
