/* Per-thread state for Linux threading support
 *
 * Copyright 2026 elfuse contributors
 * Copyright 2025 Moritz Angermann, zw3rk pte. ltd.
 * SPDX-License-Identifier: Apache-2.0
 *
 * Maintains a table of guest threads. Each thread has its own HVF vCPU running
 * on a dedicated host pthread. The main thread is registered at startup; worker
 * threads are added via clone(CLONE_THREAD). A _Thread_local pointer provides
 * O(1) access to the current thread's entry from any syscall handler.
 *
 * SP_EL1 allocation: each thread gets a 4KiB EL1 exception stack carved from
 * the shim data region (g->shim_data_base + 2MiB). Thread 0 (main) gets the
 * top, thread N gets offset -(N * 4096).
 */

#pragma once

#include <Hypervisor/Hypervisor.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>

#include "core/guest.h"  /* guest_t (for thread_alloc_sp_el1) */
#include "syscall/abi.h" /* linux_user_pt_regs_t */

/* Maximum number of concurrent guest threads in one VM. */
#define MAX_THREADS 64
#define MAX_DEFERRED_STACK_UNMAPS 8

/* Per-thread state. One entry per guest thread (main + workers). */
typedef struct {
    int64_t guest_tid;        /* Linux TID (unique per thread) */
    hv_vcpu_t vcpu;           /* HVF vCPU handle for this thread */
    hv_vcpu_exit_t *vexit;    /* vCPU exit info pointer */
    pthread_t host_thread;    /* macOS host thread running this vCPU */
    uint64_t clear_child_tid; /* GVA for CLONE_CHILD_CLEARTID (0=none) */
    uint64_t sp_el1;          /* Per-thread EL1 stack top (IPA) */
    int sp_el1_slot;          /* Slot index in sp_el1_allocated (-1 = none).
                               * Stored at alloc time so the free path does
                               * not need to recompute (top - sp) / 4096; the
                               * shim data block is now at high IPA and only
                               * known via guest_t. */
    int active;               /* Non-zero while thread is running.
                               * Stays int (not bool) because lock-free paths in thread.c
                               * use __atomic_load_n on this field; the 32-bit width keeps
                               * the access pattern predictable across architectures.
                               */
    /* Per-thread signal mask (POSIX requires each thread to have its own).
     * Initialized to the parent's mask on clone, modified via rt_sigprocmask.
     */
    uint64_t blocked;       /* Signal mask for this thread */
    uint64_t saved_blocked; /* Original mask saved by sigsuspend */
    bool saved_blocked_valid;

    /* Per-thread alternate signal stack (Linux sigaltstack is per-thread).
     * Fields mirror linux_stack_t layout for easy copy.
     */
    uint64_t altstack_sp;   /* Alternate signal stack pointer */
    int32_t altstack_flags; /* SS_DISABLE / 0 */
    uint64_t altstack_size; /* Alternate signal stack size */
    bool on_altstack;       /* True if currently delivering on altstack */

    /* Robust futex list head (GVA). When non-zero, thread exit walks the list
     * and sets FUTEX_OWNER_DIED on each lock word.
     */
    uint64_t robust_list_head;

    /* rseq (restartable sequences) per-thread registration state.
     * When rseq_gva != 0, the thread has a registered struct rseq.
     * Signal delivery and preemption must abort active critical sections.
     */
    uint64_t rseq_gva;       /* Guest VA of struct rseq (0 = not registered) */
    uint32_t rseq_len;       /* Length from registration */
    uint32_t rseq_signature; /* Abort signature from registration */

    /* ptrace state.
     * Used by two-process JIT architectures: the tracer attaches via
     * PTRACE_SEIZE, then uses BRK-triggered SIGTRAP + wait4 to discover
     * untranslated code on-demand. The tracee snapshots its own vCPU registers
     * before stopping and applies any tracer-written changes on resume,
     * avoiding cross-thread HVF register access (which may not be supported).
     */
    bool ptraced;                /* True if being traced */
    int64_t tracer_tid;          /* TID of tracing thread */
    bool ptrace_stopped;         /* True when in ptrace-stop */
    int ptrace_stop_sig;         /* Signal that caused the stop */
    pthread_cond_t ptrace_cond;  /* Tracee stopped -> tracer wakes */
    pthread_cond_t resume_cond;  /* Tracer CONT -> tracee wakes */
    bool ptrace_conds_inited;    /* Condvars initialized for this slot */
    int ptrace_waiters;          /* Tracers currently blocked on ptrace_cond */
    bool ptrace_cleanup_pending; /* Destroy condvars after last waiter leaves */
    int ptrace_cont_sig;         /* Signal to inject on resume (0=none) */
    linux_user_pt_regs_t ptrace_regs; /* snapshot for cross-thread access */
    bool ptrace_regs_dirty;           /* Tracer modified registers */

    /* GDB stub state.
     * Per-thread stop state for the GDB remote stub. When GDB requests a stop
     * (breakpoint, Ctrl+C, step), the thread records its stop reason here so
     * the stub can report it to GDB.
     *
     * Register access: HVF requires vCPU register reads/writes on the owning
     * thread. When stopped, the vCPU thread snapshots registers into
     * gdb_reg_snapshot; the GDB handler thread reads/writes that buffer. On
     * resume, dirty changes are applied back to the vCPU.
     */
    uint8_t gdb_reg_snapshot[788]; /* Register snapshot for GDB
                                    * Layout: 31×GPR(8) + SP(8) + PC(8)
                                    * + CPSR(4) + 32×V(16) + FPSR(4) + FPCR(4)
                                    */
    bool gdb_regs_dirty;           /* GDB handler modified snapshot */

    /* VM-clone child state.
     * For clone(CLONE_VM) without CLONE_THREAD: shares guest memory but has a
     * separate TID, is waitable via wait4, and can be ptraced.
     * Used by clone(CLONE_VM) two-process architecture.
     */
    bool is_vm_clone;   /* Waitable via wait4 */
    int64_t parent_tid; /* Parent TID for wait4 matching */
    int exit_signal;    /* Signal on exit (usually SIGCHLD) */
    bool vm_exited;     /* Child has exited */
    int vm_exit_status; /* Wait-format exit status */

    /* Guest stack range supplied by clone3(stack, stack_size).
     * elfuse uses this to avoid tearing down a still-active child stack when
     * another thread munmaps the backing range before the child is done with
     * its bootstrap stack.
     */
    uint64_t stack_map_start;
    uint64_t stack_map_end;
    uint64_t deferred_stack_unmap_starts[MAX_DEFERRED_STACK_UNMAPS];
    uint64_t deferred_stack_unmap_ends[MAX_DEFERRED_STACK_UNMAPS];
    int deferred_stack_unmap_count;
    int deferred_stack_unmap_busy;
} thread_entry_t;

typedef struct {
    thread_entry_t *thread;
    int64_t guest_tid;
    uint64_t start;
    uint64_t end;
    uint64_t deferred_starts[MAX_DEFERRED_STACK_UNMAPS];
    uint64_t deferred_ends[MAX_DEFERRED_STACK_UNMAPS];
    int deferred_count;
} thread_deferred_stack_unmap_txn_t;

/* Current thread pointer, set once per host pthread at thread start.
 * All syscall handlers can access per-thread state through this.
 */
extern _Thread_local thread_entry_t *current_thread;

/* Initialize the thread table. Call once before any thread operations. */
void thread_init(void);

/* Register the main thread (thread 0). Called from main.c after the initial
 * vCPU is created. Sets current_thread for the main thread.
 */
void thread_register_main(hv_vcpu_t vcpu,
                          hv_vcpu_exit_t *vexit,
                          int64_t tid,
                          uint64_t sp_el1);

/* Allocate a new thread table slot for the given TID.
 * Returns a pointer to the entry, or NULL if the table is full.
 * The caller must fill in vcpu, vexit, host_thread, sp_el1.
 */
thread_entry_t *thread_alloc(int64_t tid,
                             uint64_t stack_start,
                             uint64_t stack_end);

/* Mark a thread as inactive and release its table slot. */
void thread_deactivate(thread_entry_t *t);

/* Find a thread by guest TID. Returns NULL if not found. */
thread_entry_t *thread_find(int64_t tid);

/* Lock-free check: is there an active thread with this TID?
 * Returns 1 if found, 0 if not. Safe to call without holding any lock (used
 * from futex_lock_pi to avoid lock order inversion with bucket locks). May
 * return a stale true if the thread is being deactivated concurrently;
 * callers must tolerate this.
 */
int thread_tid_alive(int64_t tid);

/* Count currently active threads. */
int thread_active_count(void);

/* Fast path: return non-zero when exactly one guest thread is active. */
int thread_is_single_active(void);

/* Allocate a per-thread SP_EL1 stack and record both the IPA and the slot
 * index into t. Thread N gets the Nth 4KiB slot counting down from the top
 * of the shim data block (g->shim_data_base + 2MiB). The shim block lives
 * at high IPA computed by guest_init, so callers must pass g; the slot
 * index is stored in t->sp_el1_slot so the free path (which is reached
 * from teardown contexts that lack g) can clear the bitmask directly.
 * Returns the SP_EL1 IPA, or 0 on slot exhaustion.
 */
uint64_t thread_alloc_sp_el1(const guest_t *g, thread_entry_t *t);

/* Iterate over all active threads, calling fn(entry, ctx) for each.
 * Holds the thread table lock during iteration.
 */
void thread_for_each(void (*fn)(thread_entry_t *t, void *ctx), void *ctx);

/* Count active VM-clone threads (is_vm_clone && !vm_exited).
 * Used to detect when the last VM-clone child exits.
 */
int thread_count_active_vm_clones(void);

/* Join worker threads (all active threads except the caller).
 * Collects thread handles under the lock, then polls/joins OUTSIDE
 * the lock so workers can call thread_deactivate() to set active=0.
 * Threads still alive after ~50ms are detached (process is exiting).
 */
void thread_join_workers(void);

/* Destroy all active worker vCPUs. Called during guest_destroy to
 * ensure no vCPUs remain active before hv_vm_destroy().
 */
void thread_destroy_all_vcpus(void);

/* Interrupt all active vCPUs by calling hv_vcpus_exit().
 * Used for signal preemption: when a signal is queued while a vCPU
 * is running in a tight loop (no syscalls), this forces it to break
 * out of hv_vcpu_run so the signal can be delivered.
 */
void thread_interrupt_all(void);

/* Check if any active thread has sigbit unblocked in its signal mask.
 * Uses relaxed reads on per-thread blocked fields; false positives
 * (stale blocked=0) cause a harmless spurious interrupt; false negatives
 * (stale blocked=1) are corrected by rt_sigprocmask re-checking pending
 * signals after unblock.  Does NOT acquire thread_lock.
 */
int thread_signal_deliverable(uint64_t sigbit);

/* Fork quiesce helpers. */

/* Quiesce all sibling vCPUs for fork snapshot consistency.
 * Calls hv_vcpus_exit on all active threads except the caller,
 * then waits until they are all blocked on the fork barrier.
 * Caller must NOT hold thread_lock.
 */
void thread_quiesce_siblings(void);

/* Resume sibling vCPUs after fork snapshot is complete.
 * Clears the quiesce flag and broadcasts the fork condvar.
 */
void thread_resume_siblings(void);

/* Check if a fork quiesce is in progress. Called from the vCPU run
 * loop's HV_EXIT_REASON_CANCELED handler. If active, increments the
 * quiesced counter, blocks until the fork completes, then returns 1.
 * Returns 0 if no quiesce is active.
 */
int thread_fork_barrier_check(void);

/* Ptrace helpers. */

/* Tracee: snapshot vCPU regs, enter ptrace-stop, block until resumed.
 * Returns the signal to inject (from tracer's PTRACE_CONT), or 0.
 */
int thread_ptrace_stop(thread_entry_t *t, int sig);

/* Tracer: resume a stopped tracee with optional signal injection. */
void thread_ptrace_cont(thread_entry_t *t, int sig);

/* Tracer: wait for a ptraced or vm-clone child to stop or exit.
 * Returns child TID on success, 0 on WNOHANG with none ready,
 * or negative Linux errno. Writes wait-format status to *out_status.
 */
int64_t thread_ptrace_wait(int64_t tracer_tid,
                           int pid,
                           int *out_status,
                           int options);

/* Get the thread table mutex (needed for ptrace wait blocking). */
pthread_mutex_t *thread_get_lock(void);

/* Snapshot every active guest stack range overlapping [start, end), then
 * record a deferred-unmap entry on each one. While the transaction is live,
 * cleanup of the affected thread's deferred stack entries will block so a
 * later rollback cannot race with thread exit.
 * On success, txns[0..nranges) contains both the overlapping ranges and the
 * pre-update deferred-unmap state needed for rollback.
 * Returns the number of overlapping stack ranges, or -1 if the caller's
 * buffer is too small or any thread's deferred-unmap budget is exhausted.
 */
int thread_collect_and_defer_stack_ranges(
    uint64_t start,
    uint64_t end,
    thread_deferred_stack_unmap_txn_t *txns,
    int max_ranges);

/* Release the in-flight marker set by thread_collect_and_defer_stack_ranges()
 * after the caller has successfully completed the non-deferred munmap work.
 */
void thread_finish_deferred_stack_ranges(
    const thread_deferred_stack_unmap_txn_t *txns,
    int nranges);

/* Restore the deferred-unmap state previously captured by
 * thread_collect_and_defer_stack_ranges(), then release the in-flight marker.
 */
void thread_rollback_deferred_stack_ranges(
    const thread_deferred_stack_unmap_txn_t *txns,
    int nranges);

/* For thread exit cleanup: wait for any in-flight deferred-stack munmap
 * transaction affecting this thread to finish, then clear the live stack map
 * and snapshot the current deferred unmaps. Returns the number of entries
 * copied (capped at max_ranges).
 */
int thread_prepare_deferred_stack_unmaps_for_cleanup(thread_entry_t *t,
                                                     uint64_t *starts,
                                                     uint64_t *ends,
                                                     int max_ranges);
/* Snapshot the deferred unmap entries without modifying the thread record.
 * Returns the number of entries copied (capped at max_ranges).
 */
int thread_peek_deferred_stack_unmaps(thread_entry_t *t,
                                      uint64_t *starts,
                                      uint64_t *ends,
                                      int max_ranges);

/* Drop a single completed deferred unmap entry by exact [start, end) match.
 * Returns 1 if removed, 0 if no matching entry was found.
 */
int thread_drop_deferred_stack_unmap(thread_entry_t *t,
                                     uint64_t start,
                                     uint64_t end);

/* Forget the thread's stack range so future munmap calls do not enqueue new
 * deferred entries against this slot. Safe to call once the thread is dead.
 */
void thread_clear_stack_map(thread_entry_t *t);
