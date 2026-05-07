/* Fork/clone IPC
 *
 * Copyright 2026 elfuse contributors
 * Copyright 2025 Moritz Angermann, zw3rk pte. ltd.
 * SPDX-License-Identifier: Apache-2.0
 *
 * Implements clone via posix_spawn + IPC state transfer. macOS HVF allows only
 * one VM per process, so fork spawns a new elfuse process and serializes the
 * full VM state (registers, memory, FDs) over a socketpair.
 */

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>
#include <spawn.h>
#include <sys/spawn.h> /* POSIX_SPAWN_CLOEXEC_DEFAULT (macOS extension) */
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <dirent.h> /* fdopendir, for DIR* reconstruction in child */
#include <sys/wait.h>
#include <mach-o/dyld.h>

#include "hvutil.h"
#include "utils.h"

#include "runtime/forkipc.h"
#include "runtime/fork-state.h"
#include "runtime/futex.h"

#include "syscall/abi.h"
#include "syscall/internal.h"
#include "syscall/net.h"  /* absock namespace IPC state */
#include "syscall/poll.h" /* wakeup_pipe_signal */
#include "syscall/proc.h"
#include "syscall/proc-pidfd.h"
#include "syscall/signal.h"

#include "debug/log.h"

/* fork_child_main. */

int fork_child_main(int ipc_fd, bool verbose, int timeout_sec)
{
    /* Reinitialize logging after posix_spawn (mutex state is undefined). */
    log_init();
    if (verbose)
        log_set_level(LOG_DEBUG);

    /* Reset static process/thread/futex state before receiving the parent
     * snapshot so the incoming metadata survives child restore.
     */
    proc_init();

    /* The header fixes the IPC protocol version and the guest identity before
     * any variable-length state is trusted.
     */
    ipc_header_t hdr;
    if (fork_ipc_read_all(ipc_fd, &hdr, sizeof(hdr)) < 0) {
        log_error("fork-child: failed to read header");
        return 1;
    }
    if (hdr.magic != IPC_MAGIC_HEADER) {
        log_error("fork-child: bad magic 0x%x", hdr.magic);
        return 1;
    }
    if (hdr.version != IPC_VERSION) {
        log_error(
            "fork-child: IPC version mismatch "
            "(got %u, expected %u)",
            hdr.version, IPC_VERSION);
        return 1;
    }

    log_debug("fork-child: pid=%lld ppid=%lld", (long long) hdr.child_pid,
              (long long) hdr.parent_pid);

    /* Static process identity lives in syscall/proc.c; use its accessor so the
     * child sees the parent-assigned guest PID/PPID.
     */
    proc_set_identity(hdr.child_pid, hdr.parent_pid);
    proc_set_ids(hdr.uid, hdr.euid, hdr.suid, hdr.gid, hdr.egid, hdr.sgid);
    proc_set_nice(hdr.nice);
    absock_set_namespace_id(hdr.absock_namespace_id);
    proc_set_session(hdr.sid, hdr.pgid);

    /* Create guest memory before receiving state so all incoming offsets can
     * be bounds-checked against the negotiated guest size.
     */
    guest_t g;

    if (hdr.has_shm) {
        /* CoW fork: receive shm fd via SCM_RIGHTS, then map MAP_PRIVATE.
         * This gives the child an instant copy-on-write snapshot of the
         * parent's entire guest memory, with no region enumeration or byte
         * copying.
         */
        int shm_fd = -1, shm_count = 0;
        if (fork_ipc_recv_fds(ipc_fd, &shm_fd, 1, &shm_count) < 0 ||
            shm_count != 1) {
            log_error("fork-child: failed to receive shm fd");
            close(ipc_fd);
            return 1;
        }
        if (guest_init_from_shm(&g, shm_fd, hdr.guest_size, hdr.ipa_bits) < 0) {
            log_error("fork-child: guest_init_from_shm failed");
            close(ipc_fd);
            return 1;
        }
        log_debug("fork-child: CoW fork via shm fd");
    } else {
        /* Legacy fork copies selected guest memory regions over IPC. */
        if (guest_init(&g, hdr.guest_size, hdr.ipa_bits) < 0) {
            log_error("fork-child: failed to init guest");
            close(ipc_fd);
            return 1;
        }
    }

    /* Restore allocator/page-table cursors before mmap/brk can run in child.
     * Validate pt_pool_next and ttbr0: both must reside within the page table
     * pool [PT_POOL_BASE, PT_POOL_END). Accepting out-of-range values from IPC
     * would corrupt page table allocation or translation walks.
     */
    if (hdr.pt_pool_next < PT_POOL_BASE || hdr.pt_pool_next > PT_POOL_END) {
        log_error("fork-child: invalid pt_pool_next 0x%llx",
                  (unsigned long long) hdr.pt_pool_next);
        guest_destroy(&g);
        close(ipc_fd);
        return 1;
    }
    uint64_t ttbr0_off = hdr.ttbr0 - g.ipa_base;
    if (ttbr0_off < PT_POOL_BASE || ttbr0_off >= PT_POOL_END) {
        log_error("fork-child: invalid ttbr0 0x%llx",
                  (unsigned long long) hdr.ttbr0);
        guest_destroy(&g);
        close(ipc_fd);
        return 1;
    }
    g.brk_base = hdr.brk_base;
    g.brk_current = hdr.brk_current;
    g.stack_base = hdr.stack_base;
    g.stack_top = hdr.stack_top;
    g.mmap_next = hdr.mmap_next;
    g.mmap_end = hdr.mmap_end;
    g.pt_pool_next = hdr.pt_pool_next;
    g.ttbr0 = hdr.ttbr0;
    g.mmap_rx_next = hdr.mmap_rx_next;
    g.mmap_rx_end = hdr.mmap_rx_end;

    /* Register state is the fork return frame captured from the parent vCPU. */
    ipc_registers_t regs;
    if (fork_ipc_read_all(ipc_fd, &regs, sizeof(regs)) < 0) {
        log_error("fork-child: failed to read registers");
        guest_destroy(&g);
        return 1;
    }

    if (fork_ipc_recv_memory_regions(ipc_fd, &g) < 0) {
        guest_destroy(&g);
        return 1;
    }

    if (fork_ipc_recv_fd_table(ipc_fd, &g) < 0) {
        guest_destroy(&g);
        return 1;
    }

    signal_state_t sig;
    if (fork_ipc_recv_process_state(ipc_fd, &g, &sig) < 0) {
        guest_destroy(&g);
        return 1;
    }
    /* POSIX: "Signals pending to the parent shall not be pending to the child."
     * Clear pending bitmask and RT queue before applying state.
     * signal_set_state() is deferred until after thread_register_main()
     * so that current_thread is non-NULL and per-thread state (blocked mask,
     * altstack) is properly restored.
     */
    sig.pending = 0;
    memset(sig.rt_queue, 0, sizeof(sig.rt_queue));
    memset(sig.rt_head, 0, sizeof(sig.rt_head));
    memset(sig.rt_info, 0, sizeof(sig.rt_info));

    /* execve in the child needs the shim bytes after guest_reset clears memory.
     */
    /* Close IPC socket */
    close(ipc_fd);

    /* Create the child vCPU only after all inherited state is available. */
    hv_vcpu_t vcpu;
    hv_vcpu_exit_t *vexit;
    HV_CHECK(hv_vcpu_create(&vcpu, &vexit, NULL));
    g.vcpu = vcpu;
    g.exit = vexit;

    /* Restore system registers. For fork children, the child enables the MMU
     * directly via hv_vcpu_set_sys_reg (rather than going through the shim
     * entry point) because:
     * 1. The page tables are already set up (copied from parent via IPC)
     * 2. The shim entry zeros ALL GPRs before ERET, which would destroy
     *    callee-saved registers (X19-X28, FP, LR) that the guest expects
     *    preserved across the clone() syscall
     * 3. The child can restore the exact parent GPR state and only set X0=0
     */
    HV_CHECK(hv_vcpu_set_sys_reg(vcpu, HV_SYS_REG_VBAR_EL1, regs.vbar_el1));
    HV_CHECK(hv_vcpu_set_sys_reg(vcpu, HV_SYS_REG_MAIR_EL1, regs.mair_el1));
    HV_CHECK(hv_vcpu_set_sys_reg(vcpu, HV_SYS_REG_TCR_EL1, regs.tcr_el1));
    HV_CHECK(hv_vcpu_set_sys_reg(vcpu, HV_SYS_REG_TTBR0_EL1, regs.ttbr0_el1));
    HV_CHECK(hv_vcpu_set_sys_reg(vcpu, HV_SYS_REG_CPACR_EL1, regs.cpacr_el1));
    HV_CHECK(hv_vcpu_set_sys_reg(vcpu, HV_SYS_REG_SP_EL0, regs.sp_el0));
    HV_CHECK(hv_vcpu_set_sys_reg(vcpu, HV_SYS_REG_SP_EL1, regs.sp_el1));
    HV_CHECK(hv_vcpu_set_sys_reg(vcpu, HV_SYS_REG_TPIDR_EL0, regs.tpidr_el0));

    /* Enable MMU directly (page tables already in guest memory from IPC).
     * SCTLR must include MMU-enable (M), caches (C, I), RES1 bits,
     * and EL0 cache maintenance access (UCI, UCT) for JIT translators.
     */
    uint64_t sctlr_with_mmu = SCTLR_RES1 | SCTLR_M | SCTLR_C | SCTLR_I |
                              SCTLR_DZE | SCTLR_UCT | SCTLR_UCI;
    HV_CHECK(hv_vcpu_set_sys_reg(vcpu, HV_SYS_REG_SCTLR_EL1, sctlr_with_mmu));

    /* Restore all 31 GPRs from parent state, then override X0=0 (child
     * clone return value). This preserves X1-X30 exactly as they were when
     * the parent called clone(), which is required by the Linux syscall ABI
     * (especially callee-saved X19-X28, FP=X29, LR=X30).
     */
    vcpu_restore_gprs(vcpu, regs.x);
    vcpu_set_gpr(vcpu, 0, 0); /* Child gets 0 from clone */

    vcpu_restore_simd(vcpu, &regs.simd_state);

    /* Start at the clone return point in EL0 (not the shim entry).
     * ELR_EL1 points to the guest's clone return site. SPSR_EL1 has the saved
     * EL0 state. The child sets PC/CPSR for EL0t execution.
     */
    HV_CHECK(hv_vcpu_set_sys_reg(vcpu, HV_SYS_REG_ELR_EL1, regs.elr_el1));
    HV_CHECK(hv_vcpu_set_sys_reg(vcpu, HV_SYS_REG_SPSR_EL1, regs.spsr_el1));
    HV_CHECK(hv_vcpu_set_reg(vcpu, HV_REG_PC, regs.elr_el1));
    HV_CHECK(hv_vcpu_set_reg(vcpu, HV_REG_CPSR, 0)); /* EL0t */

    /* Register the fork child's main thread in the thread table.
     * Without this, current_thread is NULL and any syscall handler that
     * accesses per-thread state (signal masks, ptrace, CLONE_THREAD)
     * will dereference NULL.
     */
    thread_register_main(vcpu, vexit, hdr.child_pid, regs.sp_el1);

    /* Now that current_thread is set, apply signal state. This must happen
     * after thread_register_main() so the per-thread blocked mask and
     * altstack are properly restored to the thread entry.
     */
    signal_set_state(&sig);

    log_debug("fork-child: entering vCPU loop");

    /* The child resumes from the captured fork frame and returns 0 to EL0. */
    int exit_code = vcpu_run_loop(vcpu, vexit, &g, verbose, timeout_sec);

    guest_destroy(&g);
    return exit_code;
}

/* sys_clone. */

/* Linux clone flags */
#define LINUX_CLONE_VM 0x00000100
#define LINUX_CLONE_VFORK 0x00004000
#define LINUX_CLONE_THREAD 0x00010000
#define LINUX_CLONE_SETTLS 0x00080000
#define LINUX_CLONE_PARENT_SETTID 0x00100000
#define LINUX_CLONE_CHILD_CLEARTID 0x00200000
#define LINUX_CLONE_CHILD_SETTID 0x01000000
/* LINUX_SIGCHLD defined in syscall_signal.h (included above) */

/* CLONE_THREAD: create a new guest thread in the same VM. */

/* Arguments passed to the worker pthread. Allocated by sys_clone_thread, freed
 * by the worker after vCPU creation and register setup.
 */
typedef struct {
    thread_entry_t *thread;
    guest_t *guest;
    bool verbose;
    uint64_t child_stack, flags, tls;
    /* Parent system regs to copy into the new vCPU */
    uint64_t elr, spsr, vbar, ttbr0, sctlr, tcr, mair, cpacr;
    uint64_t tpidr;
    uint64_t gprs[31];
    uint64_t sp_el1;
    vcpu_simd_state_t simd_state;
} thread_create_args_t;

/* Forward declaration: worker entry runs after sys_clone_thread */
static void *thread_create_and_run(void *arg);

static int64_t sys_clone_thread(hv_vcpu_t parent_vcpu,
                                guest_t *g,
                                uint64_t flags,
                                uint64_t child_stack,
                                uint64_t ptid_gva,
                                uint64_t tls,
                                uint64_t ctid_gva,
                                bool verbose)
{
    /* Allocate guest TID */
    int64_t child_tid = proc_alloc_pid();

    /* Allocate thread table slot */
    thread_entry_t *t = thread_alloc(child_tid);
    if (!t) {
        log_error("clone_thread: thread table full");
        return -LINUX_EAGAIN;
    }

    /* Inherit parent's signal mask (POSIX: clone inherits blocked mask) */
    if (current_thread)
        t->blocked = current_thread->blocked;

    /* Allocate per-thread EL1 stack */
    uint64_t child_sp_el1 = thread_alloc_sp_el1();
    if (child_sp_el1 == 0) {
        thread_deactivate(t);
        return -LINUX_ENOMEM;
    }
    t->sp_el1 = child_sp_el1;

    /* Capture parent register state before spawning worker.
     * HVF binds vCPU to the creating thread, so the worker must call
     * hv_vcpu_create itself. The parent passes all parent state via the args.
     */
    uint64_t parent_elr, parent_spsr, parent_vbar, parent_ttbr0;
    uint64_t parent_sctlr, parent_tcr, parent_mair, parent_cpacr;
    uint64_t parent_tpidr;
    parent_elr = vcpu_get_sysreg(parent_vcpu, HV_SYS_REG_ELR_EL1);
    parent_spsr = vcpu_get_sysreg(parent_vcpu, HV_SYS_REG_SPSR_EL1);
    parent_vbar = vcpu_get_sysreg(parent_vcpu, HV_SYS_REG_VBAR_EL1);
    parent_ttbr0 = vcpu_get_sysreg(parent_vcpu, HV_SYS_REG_TTBR0_EL1);
    parent_sctlr = vcpu_get_sysreg(parent_vcpu, HV_SYS_REG_SCTLR_EL1);
    parent_tcr = vcpu_get_sysreg(parent_vcpu, HV_SYS_REG_TCR_EL1);
    parent_mair = vcpu_get_sysreg(parent_vcpu, HV_SYS_REG_MAIR_EL1);
    parent_cpacr = vcpu_get_sysreg(parent_vcpu, HV_SYS_REG_CPACR_EL1);
    parent_tpidr = vcpu_get_sysreg(parent_vcpu, HV_SYS_REG_TPIDR_EL0);

    uint64_t parent_gprs[31];
    vcpu_snapshot_gprs(parent_vcpu, parent_gprs);

    thread_create_args_t *tca = calloc(1, sizeof(thread_create_args_t));
    if (!tca) {
        thread_deactivate(t);
        return -LINUX_ENOMEM;
    }

    tca->thread = t;
    tca->guest = g;
    tca->verbose = verbose;
    tca->child_stack = child_stack;
    tca->flags = flags;
    tca->tls = tls;
    tca->elr = parent_elr;
    tca->spsr = parent_spsr;
    tca->vbar = parent_vbar;
    tca->ttbr0 = parent_ttbr0;
    tca->sctlr = parent_sctlr;
    tca->tcr = parent_tcr;
    tca->mair = parent_mair;
    tca->cpacr = parent_cpacr;
    tca->tpidr = parent_tpidr;
    memcpy(tca->gprs, parent_gprs, sizeof(parent_gprs));
    tca->sp_el1 = child_sp_el1;
    vcpu_snapshot_simd(parent_vcpu, &tca->simd_state);

    /* CLONE_PARENT_SETTID: write child TID to parent's ptid address */
    if (flags & LINUX_CLONE_PARENT_SETTID) {
        int32_t tid32 = (int32_t) child_tid;
        if (guest_write_small(g, ptid_gva, &tid32, sizeof(tid32)) < 0) {
            free(tca);
            thread_deactivate(t);
            return -LINUX_EFAULT;
        }
    }

    /* CLONE_CHILD_CLEARTID: store the address for cleanup on exit */
    if (flags & LINUX_CLONE_CHILD_CLEARTID) {
        t->clear_child_tid = ctid_gva;
    }

    /* CLONE_CHILD_SETTID: write child TID to the child's ctid address.
     * This writes into shared guest memory (visible to child thread).
     */
    if (flags & LINUX_CLONE_CHILD_SETTID) {
        int32_t tid32 = (int32_t) child_tid;
        if (guest_write_small(g, ctid_gva, &tid32, sizeof(tid32)) < 0) {
            free(tca);
            thread_deactivate(t);
            return -LINUX_EFAULT;
        }
    }

    /* Create the host pthread (joinable; exit_group joins all workers via
     * thread_join_workers_cb before process exit). Threads clean up their TID
     * address via CLONE_CHILD_CLEARTID + futex wake.
     */
    pthread_t host_thread;
    pthread_attr_t attr;
    pthread_attr_init(&attr);

    int err = pthread_create(&host_thread, &attr, thread_create_and_run, tca);
    pthread_attr_destroy(&attr);

    if (err != 0) {
        log_error("clone_thread: pthread_create failed: %s", strerror(err));
        free(tca);
        thread_deactivate(t);
        return -LINUX_EAGAIN;
    }

    t->host_thread = host_thread;

    log_debug("clone_thread: child tid=%lld created", (long long) child_tid);

    return child_tid;
}

/* Worker pthread entry: creates the HVF vCPU on this thread (required by Apple
 * HVF, since the vCPU is bound to the creating thread), configures all
 * registers from parent state, then enters the run loop. On exit, performs
 * CLONE_CHILD_CLEARTID cleanup (write 0 + FUTEX_WAKE).
 */
static void *thread_create_and_run(void *arg)
{
    thread_create_args_t *tca = (thread_create_args_t *) arg;
    thread_entry_t *t = tca->thread;
    guest_t *g = tca->guest;

    /* Create vCPU on THIS thread (HVF requirement) */
    hv_vcpu_t vcpu;
    hv_vcpu_exit_t *vexit;
    hv_return_t r = hv_vcpu_create(&vcpu, &vexit, NULL);
    if (r != HV_SUCCESS) {
        log_error("thread tid=%lld: hv_vcpu_create failed: %d",
                  (long long) t->guest_tid, (int) r);
        free(tca);
        thread_deactivate(t);
        return NULL;
    }

    t->vcpu = vcpu;
    t->vexit = vexit;

    /* Copy system registers from parent (shared page tables, same MMU config)
     */
    HV_CHECK(hv_vcpu_set_sys_reg(vcpu, HV_SYS_REG_VBAR_EL1, tca->vbar));
    HV_CHECK(hv_vcpu_set_sys_reg(vcpu, HV_SYS_REG_MAIR_EL1, tca->mair));
    HV_CHECK(hv_vcpu_set_sys_reg(vcpu, HV_SYS_REG_TCR_EL1, tca->tcr));
    HV_CHECK(hv_vcpu_set_sys_reg(vcpu, HV_SYS_REG_TTBR0_EL1, tca->ttbr0));
    HV_CHECK(hv_vcpu_set_sys_reg(vcpu, HV_SYS_REG_CPACR_EL1, tca->cpacr));

    /* MMU already on, so set SCTLR with M=1 directly (page tables exist) */
    HV_CHECK(hv_vcpu_set_sys_reg(vcpu, HV_SYS_REG_SCTLR_EL1, tca->sctlr));

    /* Per-thread SP_EL1 (each vCPU needs its own EL1 exception stack) */
    HV_CHECK(hv_vcpu_set_sys_reg(vcpu, HV_SYS_REG_SP_EL1, tca->sp_el1));

    /* SP_EL0 = child_stack (provided by clone caller) */
    HV_CHECK(hv_vcpu_set_sys_reg(vcpu, HV_SYS_REG_SP_EL0, tca->child_stack));

    /* TPIDR_EL0 = thread-local storage pointer (if CLONE_SETTLS) */
    if (tca->flags & LINUX_CLONE_SETTLS) {
        HV_CHECK(hv_vcpu_set_sys_reg(vcpu, HV_SYS_REG_TPIDR_EL0, tca->tls));
    } else {
        HV_CHECK(hv_vcpu_set_sys_reg(vcpu, HV_SYS_REG_TPIDR_EL0, tca->tpidr));
    }

    /* ELR_EL1 = clone return point (same as parent) */
    HV_CHECK(hv_vcpu_set_sys_reg(vcpu, HV_SYS_REG_ELR_EL1, tca->elr));
    HV_CHECK(hv_vcpu_set_sys_reg(vcpu, HV_SYS_REG_SPSR_EL1, tca->spsr));

    /* Copy all 31 GPRs from parent, then set X0=0 (child clone return) */
    vcpu_restore_gprs(vcpu, tca->gprs);
    vcpu_set_gpr(vcpu, 0, 0);

    vcpu_restore_simd(vcpu, &tca->simd_state);

    /* Start at clone return point in EL0 (not shim entry) */
    HV_CHECK(hv_vcpu_set_reg(vcpu, HV_REG_PC, tca->elr));
    HV_CHECK(hv_vcpu_set_reg(vcpu, HV_REG_CPSR, 0)); /* EL0t */

    bool verbose = tca->verbose;
    free(tca);

    /* Set per-thread TLS pointer and enter worker run loop */
    current_thread = t;

    log_debug("thread tid=%lld starting on vCPU", (long long) t->guest_tid);

    vcpu_run_loop(vcpu, vexit, g, verbose, 0);

    /* Robust futex cleanup: walk the robust list and set FUTEX_OWNER_DIED on
     * each held lock, then wake one waiter. Must happen before CLEARTID so
     * waiters see the died bit.
     */
    if (t->robust_list_head != 0)
        robust_list_walk(g, t);

    /* CLONE_CHILD_CLEARTID: write 0 to the address and wake one waiter.
     * This is how pthread_join works in musl: the joining thread does
     * FUTEX_WAIT on this address until it becomes 0.
     */
    if (t->clear_child_tid != 0) {
        uint32_t zero = 0;
        if (guest_write_small(g, t->clear_child_tid, &zero, sizeof(zero)) ==
            0) {
            futex_wake_one(g, t->clear_child_tid);
        } else {
            log_warn(
                "thread tid=%lld clear_child_tid "
                "write failed (gva=0x%llx)",
                (long long) t->guest_tid,
                (unsigned long long) t->clear_child_tid);
        }
    }

    log_debug("thread tid=%lld exiting", (long long) t->guest_tid);

    hv_vcpu_destroy(vcpu);
    thread_deactivate(t);

    /* When all CLONE_THREAD workers have exited and only the main thread
     * remains, interrupt its futex_wait. In real Linux, child exit delivers
     * SIGCHLD which interrupts futex_wait with -EINTR.
     * elfuse simulates this through the futex interrupt API.
     */
    if (thread_active_count() == 1) {
        log_debug(
            "last worker exited, interrupting "
            "main thread futex_wait/poll");
        futex_interrupt_request();
        wakeup_pipe_signal();
        thread_interrupt_all();
    }

    return NULL;
}

/* CLONE_VM creates a thread sharing guest memory and waitable via wait4. */

/* Worker entry for vm-clone child threads. Nearly identical to
 * thread_create_and_run but sets vm-clone exit semantics.
 */
static void *vm_clone_thread_run(void *arg);

static int64_t sys_clone_vm(hv_vcpu_t parent_vcpu,
                            guest_t *g,
                            uint64_t flags,
                            uint64_t child_stack,
                            uint64_t ptid_gva,
                            uint64_t tls,
                            uint64_t ctid_gva,
                            bool verbose)
{
    /* Allocate guest TID */
    int64_t child_tid = proc_alloc_pid();

    /* Allocate thread table slot */
    thread_entry_t *t = thread_alloc(child_tid);
    if (!t) {
        log_error("clone_vm: thread table full");
        return -LINUX_EAGAIN;
    }

    /* Mark as VM-clone child (waitable via wait4, not CLONE_THREAD) */
    t->is_vm_clone = true;
    t->parent_tid = current_thread ? current_thread->guest_tid : 0;
    t->exit_signal = (int) (flags & 0xFF); /* Low byte = exit signal */
    if (t->exit_signal == 0)
        t->exit_signal = LINUX_SIGCHLD;

    /* Inherit parent's signal mask */
    if (current_thread)
        t->blocked = current_thread->blocked;

    /* Allocate per-thread EL1 stack */
    uint64_t child_sp_el1 = thread_alloc_sp_el1();
    if (child_sp_el1 == 0) {
        thread_deactivate(t);
        return -LINUX_ENOMEM;
    }
    t->sp_el1 = child_sp_el1;

    /* Capture parent register state */
    uint64_t parent_elr = vcpu_get_sysreg(parent_vcpu, HV_SYS_REG_ELR_EL1);
    uint64_t parent_spsr = vcpu_get_sysreg(parent_vcpu, HV_SYS_REG_SPSR_EL1);
    uint64_t parent_vbar = vcpu_get_sysreg(parent_vcpu, HV_SYS_REG_VBAR_EL1);
    uint64_t parent_ttbr0 = vcpu_get_sysreg(parent_vcpu, HV_SYS_REG_TTBR0_EL1);
    uint64_t parent_sctlr = vcpu_get_sysreg(parent_vcpu, HV_SYS_REG_SCTLR_EL1);
    uint64_t parent_tcr = vcpu_get_sysreg(parent_vcpu, HV_SYS_REG_TCR_EL1);
    uint64_t parent_mair = vcpu_get_sysreg(parent_vcpu, HV_SYS_REG_MAIR_EL1);
    uint64_t parent_cpacr = vcpu_get_sysreg(parent_vcpu, HV_SYS_REG_CPACR_EL1);
    uint64_t parent_tpidr = vcpu_get_sysreg(parent_vcpu, HV_SYS_REG_TPIDR_EL0);

    uint64_t parent_gprs[31];
    vcpu_snapshot_gprs(parent_vcpu, parent_gprs);

    thread_create_args_t *tca = calloc(1, sizeof(thread_create_args_t));
    if (!tca) {
        thread_deactivate(t);
        return -LINUX_ENOMEM;
    }

    tca->thread = t;
    tca->guest = g;
    tca->verbose = verbose;
    tca->child_stack = child_stack
                           ? child_stack
                           : vcpu_get_sysreg(parent_vcpu, HV_SYS_REG_SP_EL0);
    tca->flags = flags;
    tca->tls = tls;
    tca->elr = parent_elr;
    tca->spsr = parent_spsr;
    tca->vbar = parent_vbar;
    tca->ttbr0 = parent_ttbr0;
    tca->sctlr = parent_sctlr;
    tca->tcr = parent_tcr;
    tca->mair = parent_mair;
    tca->cpacr = parent_cpacr;
    tca->tpidr = parent_tpidr;
    memcpy(tca->gprs, parent_gprs, sizeof(parent_gprs));
    tca->sp_el1 = child_sp_el1;
    vcpu_snapshot_simd(parent_vcpu, &tca->simd_state);

    /* CLONE_PARENT_SETTID: write child TID to parent's ptid address */
    if (flags & LINUX_CLONE_PARENT_SETTID) {
        int32_t tid32 = (int32_t) child_tid;
        if (guest_write_small(g, ptid_gva, &tid32, sizeof(tid32)) < 0) {
            free(tca);
            thread_deactivate(t);
            return -LINUX_EFAULT;
        }
    }

    /* CLONE_CHILD_CLEARTID: store the address for cleanup on exit */
    if (flags & LINUX_CLONE_CHILD_CLEARTID)
        t->clear_child_tid = ctid_gva;

    /* CLONE_CHILD_SETTID: write child TID to child's ctid address */
    if (flags & LINUX_CLONE_CHILD_SETTID) {
        int32_t tid32 = (int32_t) child_tid;
        if (guest_write_small(g, ctid_gva, &tid32, sizeof(tid32)) < 0) {
            free(tca);
            thread_deactivate(t);
            return -LINUX_EFAULT;
        }
    }

    /* Create the host pthread */
    pthread_t host_thread;
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);

    int err = pthread_create(&host_thread, &attr, vm_clone_thread_run, tca);
    pthread_attr_destroy(&attr);

    if (err != 0) {
        log_error("clone_vm: pthread_create failed: %s", strerror(err));
        free(tca);
        thread_deactivate(t);
        return -LINUX_EAGAIN;
    }

    t->host_thread = host_thread;

    log_debug(
        "clone_vm: child tid=%lld created "
        "(parent=%lld, flags=0x%llx)",
        (long long) child_tid, (long long) t->parent_tid,
        (unsigned long long) flags);

    return child_tid;
}

/* Worker entry for vm-clone children. Sets up vCPU, runs guest code, then marks
 * exit status for parent's wait4 to collect.
 */
static void *vm_clone_thread_run(void *arg)
{
    thread_create_args_t *tca = (thread_create_args_t *) arg;
    thread_entry_t *t = tca->thread;
    guest_t *g = tca->guest;

    /* Create vCPU on THIS thread (HVF requirement) */
    hv_vcpu_t vcpu;
    hv_vcpu_exit_t *vexit;
    hv_return_t r = hv_vcpu_create(&vcpu, &vexit, NULL);
    if (r != HV_SUCCESS) {
        log_error("vm_clone tid=%lld: hv_vcpu_create failed: %d",
                  (long long) t->guest_tid, (int) r);
        free(tca);
        thread_deactivate(t);
        return NULL;
    }

    t->vcpu = vcpu;
    t->vexit = vexit;

    /* Copy system registers from parent */
    HV_CHECK(hv_vcpu_set_sys_reg(vcpu, HV_SYS_REG_VBAR_EL1, tca->vbar));
    HV_CHECK(hv_vcpu_set_sys_reg(vcpu, HV_SYS_REG_MAIR_EL1, tca->mair));
    HV_CHECK(hv_vcpu_set_sys_reg(vcpu, HV_SYS_REG_TCR_EL1, tca->tcr));
    HV_CHECK(hv_vcpu_set_sys_reg(vcpu, HV_SYS_REG_TTBR0_EL1, tca->ttbr0));
    HV_CHECK(hv_vcpu_set_sys_reg(vcpu, HV_SYS_REG_CPACR_EL1, tca->cpacr));
    HV_CHECK(hv_vcpu_set_sys_reg(vcpu, HV_SYS_REG_SCTLR_EL1, tca->sctlr));
    HV_CHECK(hv_vcpu_set_sys_reg(vcpu, HV_SYS_REG_SP_EL1, tca->sp_el1));
    HV_CHECK(hv_vcpu_set_sys_reg(vcpu, HV_SYS_REG_SP_EL0, tca->child_stack));

    /* TLS pointer */
    if (tca->flags & LINUX_CLONE_SETTLS) {
        HV_CHECK(hv_vcpu_set_sys_reg(vcpu, HV_SYS_REG_TPIDR_EL0, tca->tls));
    } else {
        HV_CHECK(hv_vcpu_set_sys_reg(vcpu, HV_SYS_REG_TPIDR_EL0, tca->tpidr));
    }

    /* ELR_EL1 = clone return point */
    HV_CHECK(hv_vcpu_set_sys_reg(vcpu, HV_SYS_REG_ELR_EL1, tca->elr));
    HV_CHECK(hv_vcpu_set_sys_reg(vcpu, HV_SYS_REG_SPSR_EL1, tca->spsr));

    /* Copy all 31 GPRs from parent, set X0=0 (child clone return) */
    vcpu_restore_gprs(vcpu, tca->gprs);
    vcpu_set_gpr(vcpu, 0, 0);

    vcpu_restore_simd(vcpu, &tca->simd_state);

    /* Start at clone return point in EL0 */
    HV_CHECK(hv_vcpu_set_reg(vcpu, HV_REG_PC, tca->elr));
    HV_CHECK(hv_vcpu_set_reg(vcpu, HV_REG_CPSR, 0)); /* EL0t */

    bool verbose = tca->verbose;
    free(tca);

    /* Set per-thread TLS pointer and enter worker run loop */
    current_thread = t;

    log_debug("vm_clone tid=%lld starting on vCPU", (long long) t->guest_tid);

    int exit_code = vcpu_run_loop(vcpu, vexit, g, verbose, 0);

    /* CLONE_CHILD_CLEARTID cleanup */
    if (t->clear_child_tid != 0) {
        uint32_t zero = 0;
        if (guest_write_small(g, t->clear_child_tid, &zero, sizeof(zero)) ==
            0) {
            futex_wake_one(g, t->clear_child_tid);
        }
    }

    /* Mark exit status for parent's wait4 to collect.
     * vm_exit_status uses wait-format: (exit_code << 8) for normal exit.
     */
    pthread_mutex_t *lock = thread_get_lock();
    pthread_mutex_lock(lock);
    t->vm_exited = true;
    t->vm_exit_status = (exit_code & 0xFF) << 8;
    pthread_cond_broadcast(&t->ptrace_cond);
    pthread_mutex_unlock(lock);

    log_debug("vm_clone tid=%lld exiting (code=%d)", (long long) t->guest_tid,
              exit_code);

    /* Check if this was the last VM-clone child BEFORE destroying the vCPU,
     * because thread_interrupt_all needs valid vCPU handles. In real Linux,
     * child exit delivers exit_signal (SIGCHLD) which interrupts the parent's
     * futex_wait with -EINTR. The emulator simulates this by requesting
     * exit_group and interrupting all vCPUs.
     */
    int last_clone = (thread_count_active_vm_clones() == 0);

    if (last_clone) {
        log_debug("last vm_clone exited, triggering exit_group");
        proc_request_exit_group(exit_code);
        /* Interrupt all vCPUs while the current one is still valid. The main
         * thread's vCPU may be blocked in hv_vcpu_run; this forces it out so it
         * can check proc_exit_group_requested. The current vCPU is not in
         * hv_vcpu_run (loop already exited) so the exit call on it is a
         * harmless no-op.
         */
        thread_interrupt_all();
    }

    hv_vcpu_destroy(vcpu);
    /* Keep the slot active until the parent collects status with wait4.
     * The slot is freed when thread_ptrace_wait reads vm_exited.
     */

    return NULL;
}

int64_t sys_clone(hv_vcpu_t vcpu,
                  guest_t *g,
                  uint64_t flags,
                  uint64_t child_stack,
                  uint64_t ptid_gva,
                  uint64_t tls,
                  uint64_t ctid_gva,
                  bool verbose)
{
    /* CLONE_THREAD: create a new thread in the same VM (not a new process) */
    if (flags & LINUX_CLONE_THREAD) {
        return sys_clone_thread(vcpu, g, flags, child_stack, ptid_gva, tls,
                                ctid_gva, verbose);
    }

    /* CLONE_VM without CLONE_THREAD: create an in-process VM-clone child.
     * The child shares guest memory but has a separate TID and is waitable
     * via wait4/ptrace.
     */
    if ((flags & LINUX_CLONE_VM) && !(flags & LINUX_CLONE_THREAD)) {
        return sys_clone_vm(vcpu, g, flags, child_stack, ptid_gva, tls,
                            ctid_gva, verbose);
    }

    /* elfuse only supports fork-like clone (SIGCHLD) and posix_spawn-like
     * clone (CLONE_VM|CLONE_VFORK|SIGCHLD)
     */
    bool is_vfork = (flags & LINUX_CLONE_VFORK) != 0;

    log_debug("clone(flags=0x%llx, vfork=%d)", (unsigned long long) flags,
              is_vfork);

    /* socketpair provides the control channel used to transfer snapshot state
     * and SCM_RIGHTS file descriptors to the fork-child process.
     */
    int sock_fds[2];
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sock_fds) < 0) {
        log_error("clone: socketpair failed: %s", strerror(errno));
        return -LINUX_ENOMEM;
    }

    /* Spawn the same elfuse binary so the child has the same entitlement and
     * build as the parent.
     */
    char self_path[LINUX_PATH_MAX];
    uint32_t path_len = sizeof(self_path);
    if (_NSGetExecutablePath(self_path, &path_len) != 0) {
        log_error("clone: _NSGetExecutablePath failed");
        close(sock_fds[0]);
        close(sock_fds[1]);
        return -LINUX_ENOMEM;
    }

    /* The child starts in --fork-child mode and receives the inherited state
     * on the socket fd.
     */
    char fd_str[32];
    snprintf(fd_str, sizeof(fd_str), "%d", sock_fds[1]);

    /* argv is intentionally minimal; guest argv is restored later from IPC. */
    char *child_argv[6];
    int ci = 0;
    child_argv[ci++] = self_path;
    if (verbose)
        child_argv[ci++] = "--verbose";
    child_argv[ci++] = "--fork-child";
    child_argv[ci++] = fd_str;
    child_argv[ci] = NULL;

    /* Set up spawn attributes: close all inherited FDs by default.
     * POSIX_SPAWN_CLOEXEC_DEFAULT (macOS extension) marks all FDs as
     * close-on-exec in the child. Without this, ALL parent host FDs (pipes,
     * sockets, etc.) leak into the child elfuse process, wasting file
     * descriptors and potentially preventing pipe EOF detection.
     */
    posix_spawnattr_t spawn_attr;
    posix_spawnattr_init(&spawn_attr);
    posix_spawnattr_setflags(&spawn_attr, POSIX_SPAWN_CLOEXEC_DEFAULT);

    /* Set up file actions: explicitly inherit only needed FDs.
     * With CLOEXEC_DEFAULT, everything is closed unless elfuse opts in.
     */
    posix_spawn_file_actions_t file_actions;
    posix_spawn_file_actions_init(&file_actions);
    posix_spawn_file_actions_addinherit_np(&file_actions, STDIN_FILENO);
    posix_spawn_file_actions_addinherit_np(&file_actions, STDOUT_FILENO);
    posix_spawn_file_actions_addinherit_np(&file_actions, STDERR_FILENO);
    posix_spawn_file_actions_addinherit_np(&file_actions, sock_fds[1]);

    extern char **environ;
    pid_t child_host_pid;
    int spawn_ret = posix_spawn(&child_host_pid, self_path, &file_actions,
                                &spawn_attr, child_argv, environ);
    posix_spawn_file_actions_destroy(&file_actions);
    posix_spawnattr_destroy(&spawn_attr);

    if (spawn_ret != 0) {
        log_error("clone: posix_spawn failed: %s", strerror(spawn_ret));
        close(sock_fds[0]);
        close(sock_fds[1]);
        return -LINUX_ENOMEM;
    }

    /* The parent keeps only its end of the control channel. */
    close(sock_fds[1]);
    int ipc_sock = sock_fds[0];

    /* Allocate guest PID before serialization so the child header carries its
     * final Linux-visible identity.
     */
    int64_t child_guest_pid = proc_alloc_pid();

    /* Quiesce sibling vCPUs for snapshot consistency.
     * In multithreaded guests, sibling vCPUs may be actively mutating guest
     * memory during the fork snapshot (CoW or legacy IPC copy).
     * Without quiescing them, the child process can receive a torn snapshot
     * with partially-updated data structures. This matches POSIX fork semantics
     * where only the calling thread survives.
     */
    thread_quiesce_siblings();

    /* Determine if elfuse can use the CoW (shm) fast path.
     * If shm_fd >= 0, elfuse freezes a snapshot via MAP_PRIVATE and sends the
     * shm fd to the child. Otherwise fall back to region-by-region copy.
     */
    /* Use CoW fork when the guest has file-backed shared memory. */
    bool use_shm = (g->shm_fd >= 0);

    /* elfuse does not remap the parent to MAP_PRIVATE here. The parent
     * stays on MAP_SHARED; its vCPU continues writing to the shared file.
     * The child maps MAP_PRIVATE, getting a CoW snapshot.
     *
     * This is safe because the IPC is synchronous: the child maps MAP_PRIVATE
     * before the parent's vCPU resumes. After that, the child's CoW pages are
     * frozen (child writes are private, parent writes to MAP_SHARED do not
     * affect CoW'd child pages).
     *
     * an earlier implementation tried remapping the parent to MAP_PRIVATE here,
     * but that breaks HVF: hv_vm_map caches the host VA->PA mapping, and
     * MAP_FIXED remap invalidates it. The parent's vCPU then reads stale
     * memory, causing corrupted syscall data (EFAULT on writev).
     */

    /* Snapshot of the semantic region array, populated after the memory dump
     * but before sibling vCPUs resume. Declared up front so all goto paths to
     * fail_snapshot can free it unconditionally.
     */
    guest_region_t *regions_snapshot = NULL;

    /* Header */
    ipc_header_t hdr = {
        .magic = IPC_MAGIC_HEADER,
        .version = IPC_VERSION,
        .ipa_bits = g->ipa_bits,
        .has_shm = (uint32_t) use_shm,
        .child_pid = child_guest_pid,
        .parent_pid = proc_get_pid(),
        .guest_size = g->guest_size,
        .brk_base = g->brk_base,
        .brk_current = g->brk_current,
        .stack_base = g->stack_base,
        .stack_top = g->stack_top,
        .mmap_next = g->mmap_next,
        .mmap_end = g->mmap_end,
        .pt_pool_next = g->pt_pool_next,
        .ttbr0 = g->ttbr0,
        .mmap_rx_next = g->mmap_rx_next,
        .mmap_rx_end = g->mmap_rx_end,
        .uid = proc_get_uid(),
        .euid = proc_get_euid(),
        .suid = proc_get_suid(),
        .gid = proc_get_gid(),
        .egid = proc_get_egid(),
        .sgid = proc_get_sgid(),
        .nice = proc_get_nice(),
        .absock_namespace_id = absock_get_namespace_id(),
        .sid = proc_get_sid(),
        .pgid = proc_get_pgid(),
    };
    if (fork_ipc_write_all(ipc_sock, &hdr, sizeof(hdr)) < 0) {
        log_error("clone: failed to send header");
        goto fail_snapshot;
    }

    /* CoW path: sync MAP_SHARED file overlays back into shm_fd before
     * sending it to the child. The parent's host VA at each overlay
     * region maps the overlay file, not shm_fd, so shm_fd's content at
     * those IPAs is stale (typically zero). The child's MAP_PRIVATE
     * snapshot would expose that stale data at the overlay IPAs. Copy
     * the live overlay bytes into shm_fd at the matching offsets so the
     * child snapshot reflects the parent's view at fork time. Live
     * cross-fork MAP_SHARED coherence (parent and child both seeing
     * subsequent writes through the same file) is left to the cross-fork
     * coherence TODO; this fix only avoids the stale-snapshot regression.
     */
    if (use_shm) {
        for (int i = 0; i < g->nregions; i++) {
            const guest_region_t *r = &g->regions[i];
            if (!r->overlay_active)
                continue;
            uint64_t len = r->end - r->start;
            const uint8_t *src = (const uint8_t *) g->host_base + r->start;
            uint64_t off = r->start;
            while (len > 0) {
                size_t chunk = len > (uint64_t) SSIZE_MAX ? (size_t) SSIZE_MAX
                                                          : (size_t) len;
                ssize_t nw = pwrite(g->shm_fd, src, chunk, (off_t) off);
                if (nw < 0) {
                    if (errno == EINTR)
                        continue;
                    log_error("clone: shm overlay sync pwrite failed: %s",
                              strerror(errno));
                    goto fail_snapshot;
                }
                if (nw == 0) {
                    log_error("clone: shm overlay sync pwrite returned 0");
                    goto fail_snapshot;
                }
                src += nw;
                off += (uint64_t) nw;
                len -= (uint64_t) nw;
            }
        }
        if (fork_ipc_send_fds(ipc_sock, &g->shm_fd, 1) < 0) {
            log_error("clone: failed to send shm fd");
            goto fail_snapshot;
        }
    }

    /* Registers: capture current vCPU state */
    ipc_registers_t regs = {0};
    regs.elr_el1 = vcpu_get_sysreg(vcpu, HV_SYS_REG_ELR_EL1);
    regs.sp_el0 = vcpu_get_sysreg(vcpu, HV_SYS_REG_SP_EL0);
    regs.spsr_el1 = vcpu_get_sysreg(vcpu, HV_SYS_REG_SPSR_EL1);
    regs.vbar_el1 = vcpu_get_sysreg(vcpu, HV_SYS_REG_VBAR_EL1);
    regs.ttbr0_el1 = vcpu_get_sysreg(vcpu, HV_SYS_REG_TTBR0_EL1);
    regs.sctlr_el1 = vcpu_get_sysreg(vcpu, HV_SYS_REG_SCTLR_EL1);
    regs.tcr_el1 = vcpu_get_sysreg(vcpu, HV_SYS_REG_TCR_EL1);
    regs.mair_el1 = vcpu_get_sysreg(vcpu, HV_SYS_REG_MAIR_EL1);
    regs.cpacr_el1 = vcpu_get_sysreg(vcpu, HV_SYS_REG_CPACR_EL1);
    regs.tpidr_el0 = vcpu_get_sysreg(vcpu, HV_SYS_REG_TPIDR_EL0);
    regs.sp_el1 = vcpu_get_sysreg(vcpu, HV_SYS_REG_SP_EL1);
    vcpu_snapshot_gprs(vcpu, regs.x);

    vcpu_snapshot_simd(vcpu, &regs.simd_state);

    if (fork_ipc_write_all(ipc_sock, &regs, sizeof(regs)) < 0) {
        log_error("clone: failed to send registers");
        goto fail_snapshot;
    }
    if (fork_ipc_send_memory_regions(ipc_sock, g, use_shm) < 0)
        goto fail_snapshot;

    /* Snapshot the semantic region array before resuming siblings.
     * Siblings may mmap/munmap/mprotect after resume, so the code needs a
     * stable copy for the IPC send. Heap-allocated because
     * GUEST_MAX_REGIONS * sizeof(guest_region_t) exceeds safe
     * stack limits on worker threads (512KiB default).
     */
    int nregions_snapshot = g->nregions;
    size_t snap_sz = (size_t) nregions_snapshot * sizeof(guest_region_t);
    if (nregions_snapshot > 0) {
        regions_snapshot = malloc(snap_sz);
        if (!regions_snapshot) {
            thread_resume_siblings();
            close(ipc_sock);
            return -LINUX_ENOMEM;
        }
        memcpy(regions_snapshot, g->regions, snap_sz);
    }

    if (fork_ipc_send_fd_table(ipc_sock) < 0)
        goto fail_snapshot;

    /* Resume sibling vCPUs now that the memory snapshot, semantic region
     * snapshot, and FD snapshot have been serialized.
     */
    thread_resume_siblings();

    uint32_t num_guest_regions = (uint32_t) nregions_snapshot;
    if (fork_ipc_send_process_state(ipc_sock, regions_snapshot,
                                    num_guest_regions) < 0)
        goto fail_ipc;

    close(ipc_sock);

    /* After CoW fork, parent stays on MAP_SHARED because no remap was done.
     * The shm fd is kept open so subsequent forks can also use CoW.
     * The child has its own MAP_PRIVATE view of the same file.
     */

    /* Register after successful IPC so wait4/waitid can observe the child. */
    proc_register_child(child_host_pid, child_guest_pid);

    /* CLONE_VFORK suspends the parent until the child exits or execs. The
     * emulator cannot observe guest exec completion across the helper process,
     * so it waits for the helper to exit.
     */
    if (is_vfork) {
        int status;
        waitpid(child_host_pid, &status, 0);

        /* Publish the vfork child status for later wait calls. */
        proc_mark_child_exited(child_host_pid, status);
    }

    log_debug("clone: child pid=%lld (host=%d)", (long long) child_guest_pid,
              child_host_pid);

    free(regions_snapshot);
    return child_guest_pid;

fail_ipc:
    free(regions_snapshot);
    close(ipc_sock);
    return -LINUX_ENOMEM;

fail_snapshot:
    free(regions_snapshot);
    thread_resume_siblings();
    close(ipc_sock);
    return -LINUX_ENOMEM;
}

/* clone3: extended clone with clone_args struct. */

/* Linux clone_args layout (kernel v5.3+, extensible).
 * Fields beyond the caller-provided size are treated as zero.
 */
struct linux_clone_args {
    uint64_t flags, pidfd;
    uint64_t child_tid, parent_tid;
    uint64_t exit_signal, stack, stack_size, tls, set_tid, set_tid_size, cgroup;
};

#define CLONE_ARGS_SIZE_VER0 64 /* v5.3: first 8 fields (flags..tls) */

/* Unsupported clone3 flags: reject early rather than silently ignoring. */
#define LINUX_CLONE_PIDFD 0x00001000
#define LINUX_CLONE_INTO_CGROUP 0x200000000ULL
#define LINUX_CLONE_NEWNS 0x00020000
#define LINUX_CLONE_NEWCGROUP 0x02000000
#define LINUX_CLONE_NEWUTS 0x04000000
#define LINUX_CLONE_NEWIPC 0x08000000
#define LINUX_CLONE_NEWUSER 0x10000000
#define LINUX_CLONE_NEWPID 0x20000000
#define LINUX_CLONE_NEWNET 0x40000000
#define LINUX_CLONE_NEWTIME 0x00000080

#define LINUX_CLONE3_NS_FLAGS                                         \
    (LINUX_CLONE_NEWNS | LINUX_CLONE_NEWCGROUP | LINUX_CLONE_NEWUTS | \
     LINUX_CLONE_NEWIPC | LINUX_CLONE_NEWUSER | LINUX_CLONE_NEWPID |  \
     LINUX_CLONE_NEWNET | LINUX_CLONE_NEWTIME)

int64_t sys_clone3(hv_vcpu_t vcpu,
                   guest_t *g,
                   uint64_t cl_args_gva,
                   uint64_t cl_args_size,
                   bool verbose)
{
    /* Validate size: must be at least the v5.3 minimum and not absurdly large
     */
    if (cl_args_size < CLONE_ARGS_SIZE_VER0 || cl_args_size > 4096)
        return -LINUX_EINVAL;

    /* Guard against guest address overflow when reading clone_args */
    if (cl_args_gva > UINT64_MAX - cl_args_size)
        return -LINUX_EFAULT;

    /* Read clone_args from guest memory, zero-extending beyond caller's size */
    struct linux_clone_args ca = {0};
    size_t read_size = cl_args_size < sizeof(ca) ? cl_args_size : sizeof(ca);
    if (guest_read(g, cl_args_gva, &ca, read_size) < 0)
        return -LINUX_EFAULT;

    /* If the caller provided a struct larger than elfuse knows about, verify
     * the unknown tail is all zeros (forward compatibility per kernel rule)
     */
    if (cl_args_size > sizeof(ca)) {
        uint64_t tail_start = cl_args_gva + sizeof(ca);
        uint64_t tail_size = cl_args_size - sizeof(ca);
        for (uint64_t off = 0; off < tail_size; off += 8) {
            uint64_t word = 0;
            size_t chunk = (tail_size - off < 8) ? (tail_size - off) : 8;
            if (guest_read(g, tail_start + off, &word, chunk) < 0)
                return -LINUX_EFAULT;
            if (word != 0)
                return -LINUX_E2BIG;
        }
    }

    /* In clone3, the CSIGNAL low byte of flags must be zero because the exit
     * signal is carried exclusively in the exit_signal field.
     */
    if (ca.flags & 0xff)
        return -LINUX_EINVAL;

    /* Reject unsupported features */
    if (ca.flags & LINUX_CLONE_INTO_CGROUP)
        return -LINUX_EINVAL; /* cgroups not implemented */
    if (ca.flags & LINUX_CLONE3_NS_FLAGS)
        return -LINUX_EINVAL; /* namespaces not implemented */
    if (ca.set_tid_size != 0)
        return -LINUX_EINVAL; /* set_tid not implemented */

    /* Validate exit_signal range (0-64 on Linux).
     * CLONE_THREAD requires exit_signal == 0 (threads do not signal on exit).
     */
    if (ca.exit_signal > 64)
        return -LINUX_EINVAL;
    if ((ca.flags & LINUX_CLONE_THREAD) && ca.exit_signal != 0)
        return -LINUX_EINVAL;

    /* Validate stack: both must be zero (fork-like) or both non-zero (thread).
     * Mismatched pairs cause SP underflow or zero-SP threads.
     */
    if ((ca.stack == 0) != (ca.stack_size == 0))
        return -LINUX_EINVAL;

    /* Merge exit_signal into flags for sys_clone compatibility.
     * clone3 moved exit_signal out of the flags field; sys_clone expects
     * it in the low byte. Safe because validation confirmed ca.flags low byte
     * is zero.
     */
    /* Strip CLONE_PIDFD before passing to sys_clone (which does not
     * understand it). Pidfd creation happens after the clone returns.
     */
    bool want_pidfd = (ca.flags & LINUX_CLONE_PIDFD) != 0;
    uint64_t flags =
        (ca.flags & ~(uint64_t) LINUX_CLONE_PIDFD) | ca.exit_signal;

    /* Compute child stack pointer.
     * clone: child_stack is the TOP of the stack (SP value).
     * clone3: stack is the BOTTOM, stack_size is the length.
     * SP = stack + stack_size (grows downward on aarch64).
     */
    uint64_t child_stack = 0;
    if (ca.stack != 0) {
        if (ca.stack_size > UINT64_MAX - ca.stack)
            return -LINUX_EINVAL;
        child_stack = ca.stack + ca.stack_size;
    }

    log_debug(
        "clone3(flags=0x%llx, exit_signal=%llu, "
        "stack=0x%llx+0x%llx, "
        "tls=0x%llx, size=%llu)",
        (unsigned long long) ca.flags, (unsigned long long) ca.exit_signal,
        (unsigned long long) ca.stack, (unsigned long long) ca.stack_size,
        (unsigned long long) ca.tls, (unsigned long long) cl_args_size);

    int64_t ret = sys_clone(vcpu, g, flags, child_stack, ca.parent_tid, ca.tls,
                            ca.child_tid, verbose);

    /* If clone succeeded and CLONE_PIDFD was requested, create a pidfd
     * for the child and write the guest FD number to ca.pidfd.
     */
    if (ret > 0 && want_pidfd && ca.pidfd != 0) {
        int pfd = pidfd_create(g, ret);
        if (pfd >= 0) {
            int32_t pfd32 = (int32_t) pfd;
            if (guest_write_small(g, ca.pidfd, &pfd32, sizeof(pfd32)) < 0) {
                /* GVA invalid; close the newly created pidfd. */
                fd_entry_t snap;
                if (fd_snapshot_and_close(pfd, &snap))
                    fd_cleanup_entry(pfd, &snap);
            }
        }
    }

    return ret;
}
