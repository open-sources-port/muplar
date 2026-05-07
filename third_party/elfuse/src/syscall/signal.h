/* Signal delivery
 *
 * Copyright 2026 elfuse contributors
 * Copyright 2025 Moritz Angermann, zw3rk pte. ltd.
 * SPDX-License-Identifier: Apache-2.0
 *
 * Linux aarch64 signal structures and delivery API. Matches the kernel's
 * arch/arm64/kernel/signal.c:setup_rt_frame() layout so that musl's
 * __restore_rt -> rt_sigreturn (SYS 139) can correctly restore state.
 */

#pragma once

#include <Hypervisor/Hypervisor.h>
#include <stdbool.h>
#include <stdint.h>
#include <sys/time.h>
#include "core/guest.h"

/* Linux signal numbers (1-based, matching kernel). */
#define LINUX_SIGHUP 1
#define LINUX_SIGINT 2
#define LINUX_SIGQUIT 3
#define LINUX_SIGILL 4
#define LINUX_SIGTRAP 5
#define LINUX_SIGABRT 6
#define LINUX_SIGBUS 7
#define LINUX_SIGFPE 8
#define LINUX_SIGKILL 9
#define LINUX_SIGUSR1 10
#define LINUX_SIGSEGV 11
#define LINUX_SIGUSR2 12
#define LINUX_SIGPIPE 13
#define LINUX_SIGALRM 14
#define LINUX_SIGTERM 15
#define LINUX_SIGSTKFLT 16
#define LINUX_SIGCHLD 17
#define LINUX_SIGCONT 18
#define LINUX_SIGSTOP 19
#define LINUX_SIGTSTP 20
#define LINUX_SIGTTIN 21
#define LINUX_SIGTTOU 22
#define LINUX_SIGURG 23
#define LINUX_SIGXCPU 24
#define LINUX_SIGXFSZ 25
#define LINUX_SIGVTALRM 26
#define LINUX_SIGPROF 27
#define LINUX_SIGWINCH 28
#define LINUX_SIGIO 29
#define LINUX_SIGPWR 30
#define LINUX_SIGSYS 31

#define LINUX_SIGRTMIN 32
#define LINUX_NSIG 64

/* Linux sigaction flags. */
#define LINUX_SA_SIGINFO 0x00000004
#define LINUX_SA_ONSTACK 0x08000000
#define LINUX_SA_NODEFER 0x40000000
#define LINUX_SA_RESETHAND 0x80000000U

/* SIG_DFL and SIG_IGN as handler addresses */
#define LINUX_SIG_DFL 0ULL
#define LINUX_SIG_IGN 1ULL

/* Signal mask operations. */
#define LINUX_SIG_BLOCK 0
#define LINUX_SIG_UNBLOCK 1
#define LINUX_SIG_SETMASK 2

/* Linux sigaction (kernel-style, aarch64). */
/* The kernel's struct sigaction for aarch64 (from
 * include/uapi/asm-generic/signal.h): sa_handler or sa_sigaction  (8 bytes)
 *   sa_flags                    (8 bytes on LP64)
 *   sa_restorer                 (8 bytes)
 *   sa_mask                     (8 bytes, single uint64_t for 64 signals)
 */
#ifdef sa_handler
#undef sa_handler /* macOS <signal.h> defines this as a macro */
#endif
typedef struct {
    uint64_t sa_handler; /* Handler address (or SIG_DFL=0 / SIG_IGN=1) */
    uint64_t sa_flags;
    uint64_t sa_restorer; /* __restore_rt trampoline (calls rt_sigreturn) */
    uint64_t sa_mask;     /* Blocked signals during handler execution */
} linux_sigaction_t;

/* Default signal dispositions. */
typedef enum {
    SIG_DISP_TERM, /* Terminate the process */
    SIG_DISP_IGN,  /* Ignore the signal */
    SIG_DISP_CORE, /* Terminate; core files are not generated */
    SIG_DISP_STOP, /* Stop the process (not supported, treat as ignore) */
    SIG_DISP_CONT, /* Continue the process (not supported, treat as ignore) */
} sig_disposition_t;

/* Linux siginfo_t (aarch64, 128 bytes). */
typedef struct {
    int32_t si_signo, si_errno, si_code, _pad0;
    /* Common Linux siginfo fields on aarch64. The union payload starts at
     * offset 16; queued RT signals carry sigval at offset 24.
     */
    int32_t si_pid, si_uid;
    uint64_t si_value;
    uint8_t _pad[128 - 32]; /* Pad to 128 bytes total */
} linux_siginfo_t;

/* si_code values */
#define LINUX_SI_USER 0

/* si_code values for SIGTRAP */
#define LINUX_TRAP_BRKPT 1 /* Process breakpoint (BRK instruction) */

/* si_code values for SIGILL */
#define LINUX_ILL_ILLOPC 1 /* Illegal opcode */

/* si_code values for SIGSEGV */
#define LINUX_SEGV_MAPERR 1 /* Address not mapped to object */
#define LINUX_SEGV_ACCERR 2 /* Invalid permissions for mapped object */

/* Linux sigcontext (aarch64). */
/* From arch/arm64/include/uapi/asm/sigcontext.h */
typedef struct {
    uint64_t fault_address;
    uint64_t regs[31]; /* X0-X30 */
    uint64_t sp;       /* SP_EL0 */
    uint64_t pc;       /* ELR_EL1 at time of signal */
    uint64_t pstate;   /* SPSR_EL1 at time of signal */
    /* Extension space for FPSIMD, ESR, SVE contexts (4096 bytes) */
    uint8_t __reserved[4096] __attribute__((aligned(16)));
} linux_sigcontext_t;

/* Linux stack_t and sigaltstack constants. */
#define LINUX_SS_ONSTACK 1     /* Currently executing on altstack */
#define LINUX_SS_DISABLE 2     /* Altstack is disabled */
#define LINUX_MINSIGSTKSZ 5120 /* Minimum altstack size (aarch64, post-SVE) */

typedef struct {
    uint64_t ss_sp;
    int32_t ss_flags, _pad;
    uint64_t ss_size;
} linux_stack_t;

/* Linux ucontext_t (aarch64). */
typedef struct {
    uint64_t uc_flags;
    uint64_t uc_link; /* Pointer to next ucontext (always 0) */
    linux_stack_t uc_stack;
    uint64_t uc_sigmask;   /* Signal mask to restore on sigreturn */
    uint8_t _pad[128 - 8]; /* Pad __reserved in kernel to 1024 bits */
    uint8_t _align_pad[8]; /* Align uc_mcontext to Linux's 16-byte boundary */
    linux_sigcontext_t uc_mcontext;
} linux_ucontext_t;

/* Linux rt_sigframe (pushed onto guest stack). */
/* From arch/arm64/kernel/signal.c: this is what setup_rt_frame() builds. */
typedef struct {
    linux_siginfo_t info;
    linux_ucontext_t uc;
} linux_rt_sigframe_t;

/* RT signal queue. */
/* Maximum queued instances per RT signal. POSIX says at least
 * _POSIX_SIGQUEUE_MAX (32); Linux defaults to ~1024 per user.
 */
#define RT_SIGQUEUE_MAX 32
#define RT_SIGNAL_COUNT \
    (LINUX_NSIG - LINUX_SIGRTMIN + 1) /* 33 signals: 32-64 */

typedef struct {
    int signum;
    int32_t si_code, si_pid;
    uint32_t si_uid;
    int32_t si_int;
    uint64_t si_ptr;
} signal_rt_info_t;

/* Signal state. */
typedef struct {
    linux_sigaction_t actions[LINUX_NSIG]; /* Per-signal handler state */
    uint64_t pending;                      /* Bitmask of pending signals */
    uint64_t blocked;                      /* Bitmask of blocked signals */
    uint64_t saved_blocked;                /* Original mask before sigsuspend */
    bool saved_blocked_valid;              /* True if saved_blocked is set */
    linux_stack_t altstack; /* Alternate signal stack (sigaltstack) */
    bool on_altstack;       /* True if currently delivering on altstack */
    /* Standard signal metadata: Linux coalesces signals 1-31, but preserves one
     * siginfo payload for the pending instance.
     */
    bool std_info_valid[LINUX_SIGRTMIN - 1];
    signal_rt_info_t std_info[LINUX_SIGRTMIN - 1];
    /* RT signal queue: count of pending instances per signal.
     * Standard signals (1-31) use the pending bitmask plus std_info[].
     * RT signals (32-64) are queued: each instance is tracked separately.
     */
    int rt_queue[RT_SIGNAL_COUNT];
    uint8_t rt_head[RT_SIGNAL_COUNT];
    signal_rt_info_t rt_info[RT_SIGNAL_COUNT][RT_SIGQUEUE_MAX];
} signal_state_t;

/* API */

/* Initialize signal state: all SIG_DFL, nothing pending/blocked. */
void signal_init(void);

/* Reset signal state for exec (POSIX requirement).
 * Handlers set to SIG_DFL (except SIG_IGN stays SIG_IGN).
 * Pending signals and signal mask are preserved.
 */
void signal_reset_for_exec(void);

/* Queue a signal for delivery. */
void signal_queue(int signum);

/* Queue an RT signal with sender metadata and payload from sigqueue. */
void signal_queue_rt(int signum,
                     int32_t si_code,
                     int32_t si_pid,
                     uint32_t si_uid,
                     int32_t si_int,
                     uint64_t si_ptr);

/* Queue a signal with explicit siginfo metadata. Standard signals preserve
 * one payload while coalesced; RT signals enqueue every instance.
 */
void signal_queue_info(int signum,
                       int32_t si_code,
                       int32_t si_pid,
                       uint32_t si_uid,
                       int32_t si_int,
                       uint64_t si_ptr);

/* Set fault info for the next signal delivery. When set, signal_deliver()
 * populates si_code, si_addr, fault_address, and ESR context from these
 * values instead of using the default SI_USER/si_pid fields. Consumed
 * (cleared) after one delivery. Used for synchronous faults: BRK->SIGTRAP,
 * SIGSEGV, etc. The esr parameter is the raw ESR_EL1 value; if non-zero,
 * an esr_context block is appended to __reserved after FPSIMD.
 */
void signal_set_fault_info(int si_code, uint64_t addr, uint64_t esr);

/* Check if any unblocked signal is pending. */
int signal_pending(void);

/* Deliver the highest-priority pending unblocked signal to the guest.
 * Builds an rt_sigframe on the guest stack and redirects vCPU to handler.
 * Returns: 1 if signal was delivered, 0 if nothing pending,
 *         -1 if process should terminate (default TERM/CORE disposition).
 * On terminate, *exit_code is set to 128 + signum.
 */
int signal_deliver(hv_vcpu_t vcpu, guest_t *g, int *exit_code);

/* Handle rt_sigreturn (SYS 139): restore registers from rt_sigframe on
 * the guest stack. Returns SYSCALL_EXEC_HAPPENED to skip X0 writeback.
 */
int signal_rt_sigreturn(hv_vcpu_t vcpu, guest_t *g);

/* Handle rt_sigaction (SYS 134). */
int64_t signal_rt_sigaction(guest_t *g,
                            int signum,
                            uint64_t act_gva,
                            uint64_t oldact_gva,
                            uint64_t sigsetsize);

/* Handle rt_sigprocmask (SYS 135). */
int64_t signal_rt_sigprocmask(guest_t *g,
                              int how,
                              uint64_t set_gva,
                              uint64_t oldset_gva,
                              uint64_t sigsetsize);

/* Handle rt_sigsuspend (SYS 133). */
int64_t signal_rt_sigsuspend(guest_t *g,
                             uint64_t mask_gva,
                             uint64_t sigsetsize);

/* Handle rt_sigpending (SYS 136). */
int64_t signal_rt_sigpending(guest_t *g, uint64_t set_gva, uint64_t sigsetsize);

/* Handle sigaltstack (SYS 132). ss_gva is pointer to new stack_t (or 0),
 * old_ss_gva is pointer to receive current stack_t (or 0).
 */
int64_t signal_sigaltstack(guest_t *g, uint64_t ss_gva, uint64_t old_ss_gva);

/* Get/set signal state (for fork IPC serialization). */
const signal_state_t *signal_get_state(void);
void signal_set_state(const signal_state_t *state);

/* Snapshot or consume pending signals for signalfd.
 * signal_peek_signalfd() snapshots up to max matching entries without
 * consuming them. signal_take_signalfd_exact() then consumes those exact
 * entries, preserving any matching signals that arrived later.
 */
size_t signal_peek_signalfd(uint64_t mask, signal_rt_info_t *out, size_t max);
size_t signal_take_signalfd_exact(const signal_rt_info_t *expected, size_t max);

/* Save and restore blocked mask for pselect6/ppoll signal mask atomicity.
 * signal_save_blocked returns the current blocked mask.
 * signal_set_blocked applies a new mask (respecting SIGKILL/SIGSTOP).
 * signal_restore_blocked restores a previously saved mask.
 */
uint64_t signal_save_blocked(void);
void signal_set_blocked(uint64_t mask);
void signal_restore_blocked(uint64_t saved);

/* Guest ITIMER_REAL emulation.
 * These emulate the guest's setitimer(ITIMER_REAL) internally rather than
 * forwarding to the host, because macOS shares alarm() and setitimer() as
 * the same underlying timer, and elfuse needs alarm() for its vCPU timeout.
 */

/* Set the guest's ITIMER_REAL timer. value/interval are relative durations.
 * old_value/old_interval receive the previous timer state (may be NULL).
 */
void signal_set_itimer(const struct timeval *value,
                       const struct timeval *interval,
                       struct timeval *old_value,
                       struct timeval *old_interval);

/* Get the guest's ITIMER_REAL remaining time and interval. */
void signal_get_itimer(struct timeval *value, struct timeval *interval);

/* Set/get ITIMER_VIRTUAL (which=1) or ITIMER_PROF (which=2). */
void signal_set_itimer_virt(int which,
                            const struct timeval *value,
                            const struct timeval *interval,
                            struct timeval *old_value,
                            struct timeval *old_interval);
void signal_get_itimer_virt(int which,
                            struct timeval *value,
                            struct timeval *interval);

/* Check if any guest itimer has expired; queue signals as needed.
 * Called from the vCPU loop after each syscall.
 */
void signal_check_timer(void);
