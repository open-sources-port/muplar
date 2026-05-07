/* signalfd read semantics hardening
 *
 * Copyright 2026 elfuse contributors
 * SPDX-License-Identifier: Apache-2.0
 *
 * Covers:
 *   1. RT signal multiplicity: each sigqueue/rt_tgsigqueueinfo enqueues a
 *      distinct instance with its own si_int payload, returned in FIFO
 *      order without coalescing.
 *   2. Standard signals (1-31) coalesce -- multiple kill()s produce one
 *      signalfd record (kernel parity).
 *   3. ssi_int / ssi_ptr round-trip via sigqueue() (rt_sigqueueinfo) and
 *      direct rt_tgsigqueueinfo.
 *   4. SIGRTMAX (signum 64) is reachable via signalfd (regression for the
 *      off-by-one that excluded signum == LINUX_NSIG from the collect /
 *      take loops).
 *   5. signalfd's own mask is the only filter -- per-thread blocked mask
 *      is intentionally not consulted, matching Linux semantics.
 */

#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/signalfd.h>
#include <sys/syscall.h>
#include <sys/types.h>

#include "test-harness.h"

int passes = 0, fails = 0;

#ifndef SYS_rt_tgsigqueueinfo
#define SYS_rt_tgsigqueueinfo 240
#endif

#ifndef SYS_rt_sigqueueinfo
#define SYS_rt_sigqueueinfo 138
#endif

/* siginfo_t crosses both glibc and musl, but si_value layouts differ.
 * Build the kernel-shaped buffer by hand so the test stays libc-agnostic.
 */
static void build_kernel_siginfo(int sig,
                                 int code,
                                 pid_t sender_pid,
                                 uid_t sender_uid,
                                 int payload_int,
                                 void *payload_ptr,
                                 unsigned char out[128])
{
    memset(out, 0, 128);
    int32_t s32;
    uint64_t u64;
    s32 = sig;
    memcpy(out + 0, &s32, 4);
    s32 = 0;
    memcpy(out + 4, &s32, 4); /* si_errno */
    s32 = code;
    memcpy(out + 8, &s32, 4);
    /* offset 12 is _pad0 (or part of _sifields alignment). Linux's _sifields
     * starts at offset 16 on aarch64; for SI_QUEUE the layout there is:
     *   si_pid (4) si_uid (4) si_value (8)
     */
    s32 = sender_pid;
    memcpy(out + 16, &s32, 4);
    s32 = sender_uid;
    memcpy(out + 20, &s32, 4);
    s32 = payload_int;
    memcpy(out + 24, &s32, 4);
    /* Kernel ignores the upper 4 bytes of si_value's int form, but writes the
     * pointer form into the full 8-byte slot at offset 24 for sigval_t. The
     * pointer goes into the low 8 bytes so signal_queue_rt() reads either
     * representation correctly.
     */
    u64 = (uint64_t) (uintptr_t) payload_ptr;
    memcpy(out + 24, &u64, 8);
    /* If both int and ptr are set, ptr wins because it overlaps. Tests pick
     * one or the other.
     */
    if (payload_ptr == NULL) {
        s32 = payload_int;
        memcpy(out + 24, &s32, 4);
    }
}

static int raw_rt_tgsigqueueinfo(pid_t tgid,
                                 pid_t tid,
                                 int sig,
                                 const unsigned char info[128])
{
    return (int) syscall(SYS_rt_tgsigqueueinfo, tgid, tid, sig, info);
}

static int raw_rt_sigqueueinfo(pid_t pid, int sig, const void *info)
{
    return (int) syscall(SYS_rt_sigqueueinfo, pid, sig, info);
}

static void test_rt_multiplicity(void)
{
    TEST("RT multiplicity FIFO + payload");

    int sig = SIGRTMIN + 1;
    sigset_t mask;
    sigemptyset(&mask);
    sigaddset(&mask, sig);
    sigprocmask(SIG_BLOCK, &mask, NULL);

    int fd = signalfd(-1, &mask, SFD_NONBLOCK);
    if (fd < 0) {
        FAIL("signalfd");
        return;
    }

    const int payloads[] = {0x1111, 0x2222, 0x3333};
    const int N = sizeof(payloads) / sizeof(payloads[0]);
    pid_t pid = getpid();
    for (int i = 0; i < N; i++) {
        unsigned char info[128];
        /* SI_QUEUE == -1 is the kernel marker for sigqueue-style payload. */
        build_kernel_siginfo(sig, -1, pid, getuid(), payloads[i], NULL, info);
        if (raw_rt_tgsigqueueinfo(pid, pid, sig, info) != 0) {
            close(fd);
            FAIL("rt_tgsigqueueinfo");
            return;
        }
    }

    struct signalfd_siginfo buf[4];
    memset(buf, 0, sizeof(buf));
    ssize_t r = read(fd, buf, sizeof(buf));
    close(fd);

    if (r != (ssize_t) (N * sizeof(buf[0]))) {
        printf("FAIL: read returned %zd, expected %zu\n", r,
               N * sizeof(buf[0]));
        fails++;
        return;
    }
    for (int i = 0; i < N; i++) {
        if (buf[i].ssi_signo != (uint32_t) sig) {
            printf("FAIL: record %d ssi_signo=%u, expected %d\n", i,
                   buf[i].ssi_signo, sig);
            fails++;
            return;
        }
        if (buf[i].ssi_int != payloads[i]) {
            printf("FAIL: record %d ssi_int=0x%x, expected 0x%x\n", i,
                   buf[i].ssi_int, payloads[i]);
            fails++;
            return;
        }
    }
    PASS();
}

static void test_standard_coalesces(void)
{
    TEST("standard signals coalesce");

    sigset_t mask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGUSR1);
    sigprocmask(SIG_BLOCK, &mask, NULL);

    int fd = signalfd(-1, &mask, SFD_NONBLOCK);
    if (fd < 0) {
        FAIL("signalfd");
        return;
    }

    /* Three kill()s should produce exactly one signalfd record (Linux
     * coalesces standard signals on the pending bitmask).
     */
    kill(getpid(), SIGUSR1);
    kill(getpid(), SIGUSR1);
    kill(getpid(), SIGUSR1);

    struct signalfd_siginfo buf[4];
    memset(buf, 0, sizeof(buf));
    ssize_t r = read(fd, buf, sizeof(buf));
    if (r != (ssize_t) sizeof(buf[0])) {
        printf("FAIL: expected one record (%zu bytes), got %zd\n",
               sizeof(buf[0]), r);
        close(fd);
        fails++;
        return;
    }
    if (buf[0].ssi_signo != (uint32_t) SIGUSR1) {
        printf("FAIL: ssi_signo=%u\n", buf[0].ssi_signo);
        close(fd);
        fails++;
        return;
    }
    /* Second read drains nothing -- pending bit cleared. */
    errno = 0;
    ssize_t r2 = read(fd, buf, sizeof(buf));
    close(fd);
    if (r2 != -1 || errno != EAGAIN) {
        FAIL("expected EAGAIN on follow-up read");
        return;
    }
    PASS();
}

static void test_sigrtmax_reachable(void)
{
    /* SIGRTMAX (64 on aarch64) was excluded by an off-by-one in the
     * collect/take loops (signum < LINUX_NSIG instead of <= LINUX_NSIG).
     * This test fails before the fix and passes after.
     */
    TEST("SIGRTMAX reaches signalfd");

    int sig = SIGRTMAX;
    sigset_t mask;
    sigemptyset(&mask);
    sigaddset(&mask, sig);
    sigprocmask(SIG_BLOCK, &mask, NULL);

    int fd = signalfd(-1, &mask, SFD_NONBLOCK);
    if (fd < 0) {
        FAIL("signalfd");
        return;
    }

    pid_t pid = getpid();
    unsigned char info[128];
    build_kernel_siginfo(sig, -1, pid, getuid(), 0xCAFEBABE, NULL, info);
    if (raw_rt_tgsigqueueinfo(pid, pid, sig, info) != 0) {
        close(fd);
        FAIL("rt_tgsigqueueinfo SIGRTMAX");
        return;
    }

    struct signalfd_siginfo rec;
    memset(&rec, 0, sizeof(rec));
    ssize_t r = read(fd, &rec, sizeof(rec));
    close(fd);
    if (r != (ssize_t) sizeof(rec)) {
        printf("FAIL: read returned %zd\n", r);
        fails++;
        return;
    }
    if (rec.ssi_signo != (uint32_t) sig ||
        rec.ssi_int != (int32_t) 0xCAFEBABE) {
        printf("FAIL: signo=%u int=0x%x\n", rec.ssi_signo, rec.ssi_int);
        fails++;
        return;
    }
    PASS();
}

static void test_ssi_ptr_roundtrip(void)
{
    /* sigval has separate int and ptr forms. For the ptr form the full 64
     * bits land in si_value; signalfd_siginfo exposes both ssi_int (low 32)
     * and ssi_ptr (full 64). Verify both are populated from one queued ptr.
     */
    TEST("ssi_ptr / ssi_int round-trip");

    int sig = SIGRTMIN + 2;
    sigset_t mask;
    sigemptyset(&mask);
    sigaddset(&mask, sig);
    sigprocmask(SIG_BLOCK, &mask, NULL);

    int fd = signalfd(-1, &mask, SFD_NONBLOCK);
    if (fd < 0) {
        FAIL("signalfd");
        return;
    }

    /* Use an arbitrary pointer-shaped value with a high bit set so a
     * truncating implementation drops information detectably.
     */
    void *payload = (void *) 0x0123456789ABCDEFULL;
    pid_t pid = getpid();
    unsigned char info[128];
    build_kernel_siginfo(sig, -1, pid, getuid(), 0, payload, info);
    if (raw_rt_tgsigqueueinfo(pid, pid, sig, info) != 0) {
        close(fd);
        FAIL("rt_tgsigqueueinfo");
        return;
    }

    struct signalfd_siginfo rec;
    memset(&rec, 0, sizeof(rec));
    ssize_t r = read(fd, &rec, sizeof(rec));
    close(fd);
    if (r != (ssize_t) sizeof(rec)) {
        FAIL("read short");
        return;
    }
    if (rec.ssi_ptr != (uint64_t) (uintptr_t) payload) {
        printf("FAIL: ssi_ptr=0x%llx, expected 0x%llx\n",
               (unsigned long long) rec.ssi_ptr,
               (unsigned long long) (uintptr_t) payload);
        fails++;
        return;
    }
    /* ssi_int aliases the low 32 bits of the same union. */
    if (rec.ssi_int != (int32_t) (uintptr_t) payload) {
        printf("FAIL: ssi_int=0x%x\n", rec.ssi_int);
        fails++;
        return;
    }
    PASS();
}

static void test_sender_metadata(void)
{
    /* Verify ssi_pid / ssi_uid carry the sender values supplied via
     * rt_tgsigqueueinfo's siginfo (Linux-style SI_QUEUE: caller fills
     * si_pid/si_uid; kernel does not override for negative si_code).
     */
    TEST("ssi_pid / ssi_uid from sender");

    int sig = SIGRTMIN + 3;
    sigset_t mask;
    sigemptyset(&mask);
    sigaddset(&mask, sig);
    sigprocmask(SIG_BLOCK, &mask, NULL);

    int fd = signalfd(-1, &mask, SFD_NONBLOCK);
    if (fd < 0) {
        FAIL("signalfd");
        return;
    }

    pid_t pid = getpid();
    uid_t uid = getuid();
    unsigned char info[128];
    build_kernel_siginfo(sig, -1, pid, uid, 0x55AA, NULL, info);
    if (raw_rt_tgsigqueueinfo(pid, pid, sig, info) != 0) {
        close(fd);
        FAIL("rt_tgsigqueueinfo");
        return;
    }

    struct signalfd_siginfo rec;
    memset(&rec, 0, sizeof(rec));
    ssize_t r = read(fd, &rec, sizeof(rec));
    close(fd);
    if (r != (ssize_t) sizeof(rec)) {
        FAIL("read short");
        return;
    }
    if (rec.ssi_pid != (uint32_t) pid || rec.ssi_uid != uid) {
        printf("FAIL: ssi_pid=%u (want %d), ssi_uid=%u (want %u)\n",
               rec.ssi_pid, pid, rec.ssi_uid, uid);
        fails++;
        return;
    }
    if (rec.ssi_code != -1) {
        printf("FAIL: ssi_code=%d (want -1 SI_QUEUE)\n", rec.ssi_code);
        fails++;
        return;
    }
    PASS();
}

static void test_mask_filters_only(void)
{
    /* signalfd's own mask is the sole filter: a signal blocked from
     * synchronous delivery via sigprocmask is still readable from the
     * signalfd if its mask includes the signal.
     */
    TEST("signalfd mask filters, not pthread mask");

    sigset_t pblock;
    sigemptyset(&pblock);
    sigaddset(&pblock, SIGUSR1);
    sigaddset(&pblock, SIGUSR2);
    sigprocmask(SIG_BLOCK, &pblock, NULL);

    /* signalfd only watches SIGUSR1. SIGUSR2 stays pending in the process
     * pending set after kill(), but must not appear in the read result.
     */
    sigset_t fdmask;
    sigemptyset(&fdmask);
    sigaddset(&fdmask, SIGUSR1);

    int fd = signalfd(-1, &fdmask, SFD_NONBLOCK);
    if (fd < 0) {
        FAIL("signalfd");
        return;
    }

    kill(getpid(), SIGUSR2);
    kill(getpid(), SIGUSR1);

    struct signalfd_siginfo rec[4];
    memset(rec, 0, sizeof(rec));
    ssize_t r = read(fd, rec, sizeof(rec));
    if (r != (ssize_t) sizeof(rec[0])) {
        printf("FAIL: expected one record, got %zd\n", r);
        close(fd);
        fails++;
        /* Drain SIGUSR2 to keep state clean for later tests. */
        sigset_t draino;
        sigemptyset(&draino);
        sigaddset(&draino, SIGUSR2);
        int tmp = signalfd(-1, &draino, SFD_NONBLOCK);
        if (tmp >= 0) {
            (void) read(tmp, rec, sizeof(rec));
            close(tmp);
        }
        return;
    }
    if (rec[0].ssi_signo != (uint32_t) SIGUSR1) {
        printf("FAIL: got signo=%u, expected SIGUSR1\n", rec[0].ssi_signo);
        close(fd);
        fails++;
        return;
    }
    close(fd);

    /* SIGUSR2 must still be pending -- prove by widening mask and reading. */
    sigaddset(&fdmask, SIGUSR2);
    int fd2 = signalfd(-1, &fdmask, SFD_NONBLOCK);
    if (fd2 < 0) {
        FAIL("signalfd 2");
        return;
    }
    memset(rec, 0, sizeof(rec));
    r = read(fd2, rec, sizeof(rec));
    close(fd2);
    if (r != (ssize_t) sizeof(rec[0]) ||
        rec[0].ssi_signo != (uint32_t) SIGUSR2) {
        printf("FAIL: SIGUSR2 not pending after first read (r=%zd)\n", r);
        fails++;
        return;
    }
    PASS();
}

static void test_sigqueue_libc_path(void)
{
    /* glibc / musl sigqueue() goes through SYS_rt_sigqueueinfo (138).
     * Without that wired in, sigqueue() returns ENOSYS and apps that rely
     * on POSIX queued signals (real-time apps, gdb) break. Verify the
     * libc path produces a payload-bearing record.
     */
    TEST("libc sigqueue() round-trip");

    int sig = SIGRTMIN + 4;
    sigset_t mask;
    sigemptyset(&mask);
    sigaddset(&mask, sig);
    sigprocmask(SIG_BLOCK, &mask, NULL);

    int fd = signalfd(-1, &mask, SFD_NONBLOCK);
    if (fd < 0) {
        FAIL("signalfd");
        return;
    }

    union sigval sv;
    sv.sival_int = 0x4242;
    if (sigqueue(getpid(), sig, sv) != 0) {
        close(fd);
        FAIL("sigqueue");
        return;
    }

    struct signalfd_siginfo rec;
    memset(&rec, 0, sizeof(rec));
    ssize_t r = read(fd, &rec, sizeof(rec));
    close(fd);
    if (r != (ssize_t) sizeof(rec)) {
        FAIL("read short");
        return;
    }
    if (rec.ssi_signo != (uint32_t) sig || rec.ssi_int != 0x4242) {
        printf("FAIL: signo=%u int=0x%x\n", rec.ssi_signo, rec.ssi_int);
        fails++;
        return;
    }
    PASS();
}

static void test_sigqueue_standard_metadata(void)
{
    TEST("standard sigqueue() keeps metadata");

    int sig = SIGUSR1;
    sigset_t mask;
    sigemptyset(&mask);
    sigaddset(&mask, sig);
    sigprocmask(SIG_BLOCK, &mask, NULL);

    int fd = signalfd(-1, &mask, SFD_NONBLOCK);
    if (fd < 0) {
        FAIL("signalfd");
        return;
    }

    union sigval sv;
    sv.sival_int = 0x5151;
    if (sigqueue(getpid(), sig, sv) != 0) {
        close(fd);
        FAIL("sigqueue std");
        return;
    }

    struct signalfd_siginfo rec;
    memset(&rec, 0, sizeof(rec));
    ssize_t r = read(fd, &rec, sizeof(rec));
    close(fd);
    if (r != (ssize_t) sizeof(rec)) {
        FAIL("read short");
        return;
    }
    if (rec.ssi_signo != (uint32_t) sig || rec.ssi_int != 0x5151 ||
        rec.ssi_code != SI_QUEUE || rec.ssi_pid != (uint32_t) getpid() ||
        rec.ssi_uid != (uint32_t) getuid()) {
        printf("FAIL: signo=%u int=0x%x code=%d pid=%u uid=%u\n", rec.ssi_signo,
               rec.ssi_int, rec.ssi_code, rec.ssi_pid, rec.ssi_uid);
        fails++;
        return;
    }
    PASS();
}

static void test_partial_fault_returns_partial_bytes(void)
{
    /* Partial-fault recovery (write-then-take semantics).
     *
     * Queue four RT signals (payloads 0xA1..0xA4). Place a 4-record buffer
     * so records 0 and 1 land in a valid page but records 2 and 3 cross
     * into an unmapped page. The bridge writes 2 records, hits EFAULT
     * trying to write record 2, returns partial bytes (2 * 128) -- and
     * crucially does NOT take records 2 and 3 from the rt-queue, so they
     * remain pending in original FIFO order. The follow-up read returns
     * exactly two records with payloads 0xA3 then 0xA4 (no duplication
     * of 0xA1 / 0xA2; no re-queue path that could overflow RT_SIGQUEUE_MAX
     * or desync the notification pipe).
     */
    TEST("partial fault: partial bytes + FIFO");

    int sig = SIGRTMIN + 5;
    sigset_t mask;
    sigemptyset(&mask);
    sigaddset(&mask, sig);
    sigprocmask(SIG_BLOCK, &mask, NULL);

    int fd = signalfd(-1, &mask, SFD_NONBLOCK);
    if (fd < 0) {
        FAIL("signalfd");
        return;
    }

    long page = sysconf(_SC_PAGESIZE);
    if (page <= 0)
        page = 4096;
    void *region = mmap(NULL, page * 2, PROT_READ | PROT_WRITE,
                        MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (region == MAP_FAILED) {
        close(fd);
        FAIL("mmap guard region");
        return;
    }
    if (munmap((char *) region + page, page) != 0) {
        munmap(region, page);
        close(fd);
        FAIL("munmap guard");
        return;
    }

    pid_t pid = getpid();
    const int payloads[] = {0xA1, 0xA2, 0xA3, 0xA4};
    const int N = 4;
    for (int i = 0; i < N; i++) {
        unsigned char info[128];
        build_kernel_siginfo(sig, -1, pid, getuid(), payloads[i], NULL, info);
        if (raw_rt_tgsigqueueinfo(pid, pid, sig, info) != 0) {
            munmap(region, page);
            close(fd);
            FAIL("rt_tgsigqueueinfo");
            return;
        }
    }

    char *buf = (char *) region + page - (2 * 128);
    errno = 0;
    ssize_t r = read(fd, buf, 4 * sizeof(struct signalfd_siginfo));
    if (r != (ssize_t) (2 * sizeof(struct signalfd_siginfo))) {
        printf("FAIL: expected 256 partial bytes, got r=%zd errno=%d\n", r,
               errno);
        munmap(region, page);
        close(fd);
        fails++;
        return;
    }

    struct signalfd_siginfo *delivered = (struct signalfd_siginfo *) buf;
    if (delivered[0].ssi_signo != (uint32_t) sig ||
        delivered[0].ssi_int != payloads[0] ||
        delivered[1].ssi_signo != (uint32_t) sig ||
        delivered[1].ssi_int != payloads[1]) {
        munmap(region, page);
        close(fd);
        printf("FAIL: page 1 records not [0x%x,0x%x]: got [0x%x,0x%x]\n",
               payloads[0], payloads[1], delivered[0].ssi_int,
               delivered[1].ssi_int);
        fails++;
        return;
    }
    munmap(region, page);

    /* Follow-up read into a fully-valid buffer.
     *
     * Linux dequeues the record being copied before checking copy_to_user,
     * so the record that hit EFAULT (payloads[2]) is lost; a follow-up
     * read returns one record (payloads[3]). elfuse defers the take until
     * the write succeeds, so a follow-up read returns two records
     * (payloads[2] then payloads[3]) in original FIFO order.
     *
     * Both behaviors are accepted: the contract under test is "no
     * duplication of records that already reached the guest, no
     * out-of-order delivery within whatever survives, and the last
     * queued payload is always preserved."
     */
    struct signalfd_siginfo recs[8];
    memset(recs, 0, sizeof(recs));
    ssize_t r2 = read(fd, recs, sizeof(recs));
    close(fd);
    size_t recs_returned = (size_t) r2 / sizeof(recs[0]);
    bool linux_loose =
        (r2 == (ssize_t) sizeof(recs[0])) && recs[0].ssi_int == payloads[3];
    bool elfuse_strict = (r2 == (ssize_t) (2 * sizeof(recs[0]))) &&
                         recs[0].ssi_int == payloads[2] &&
                         recs[1].ssi_int == payloads[3];
    if (!linux_loose && !elfuse_strict) {
        printf(
            "FAIL: follow-up read returned %zd bytes (%zu records); "
            "first=0x%x second=0x%x; expected either [0x%x] or [0x%x,0x%x]\n",
            r2, recs_returned, recs[0].ssi_int, recs[1].ssi_int, payloads[3],
            payloads[2], payloads[3]);
        fails++;
        return;
    }
    PASS();
}

static void test_rt_sigqueueinfo_bad_pointer_efault(void)
{
    TEST("rt_sigqueueinfo unreadable siginfo faults");

    int sig = SIGRTMIN + 6;
    sigset_t mask;
    sigemptyset(&mask);
    sigaddset(&mask, sig);
    sigprocmask(SIG_BLOCK, &mask, NULL);

    int fd = signalfd(-1, &mask, SFD_NONBLOCK);
    if (fd < 0) {
        FAIL("signalfd");
        return;
    }

    errno = 0;
    int ret = raw_rt_sigqueueinfo(getpid(), sig, (const void *) 1);
    if (ret != -1 || errno != EFAULT) {
        printf("FAIL: rt_sigqueueinfo unreadable info ret=%d errno=%d\n", ret,
               errno);
        close(fd);
        fails++;
        return;
    }

    struct signalfd_siginfo rec;
    memset(&rec, 0, sizeof(rec));
    errno = 0;
    ssize_t r = read(fd, &rec, sizeof(rec));
    if (r != -1 || errno != EAGAIN) {
        printf("FAIL: bad rt_sigqueueinfo queued a signal r=%d errno=%d\n",
               (int) r, errno);
        close(fd);
        fails++;
        return;
    }

    close(fd);
    PASS();
}

static void test_rt_sigqueueinfo_rejects_foreign_pid(void)
{
    /* rt_sigqueueinfo is a process-scoped (tgid) syscall. A pid that does
     * not name the current process must return ESRCH instead of routing
     * the signal through whichever thread happened to share the numeric
     * id. The first probe picks a pid the host kernel cannot have
     * assigned to the current guest so the call cannot collide with a
     * legitimate target.
     */
    TEST("rt_sigqueueinfo rejects foreign pid");

    unsigned char info[128];
    build_kernel_siginfo(SIGRTMIN, -1, getpid(), getuid(), 0xDEAD, NULL, info);

    errno = 0;
    int ret = raw_rt_sigqueueinfo(0x7FFFFFFE, SIGRTMIN, info);
    if (ret != -1 || errno != ESRCH) {
        printf("FAIL: foreign pid: ret=%d errno=%d (expected ESRCH)\n", ret,
               errno);
        fails++;
        return;
    }
    PASS();
}

/* Helpers for the worker-thread tid case. The worker publishes its own
 * tid via a thread-shared variable, then waits on a barrier so the main
 * thread can call rt_sigqueueinfo with that tid before the worker exits.
 */
typedef struct {
    pthread_mutex_t mtx;
    pthread_cond_t ready_cv;
    pthread_cond_t go_cv;
    pid_t worker_tid;
    bool ready;
    bool go;
} worker_sync_t;

static void *tid_worker(void *arg)
{
    worker_sync_t *s = arg;
    pthread_mutex_lock(&s->mtx);
    s->worker_tid = (pid_t) syscall(SYS_gettid);
    s->ready = true;
    pthread_cond_signal(&s->ready_cv);
    while (!s->go)
        pthread_cond_wait(&s->go_cv, &s->mtx);
    pthread_mutex_unlock(&s->mtx);
    return NULL;
}

static void test_rt_sigqueueinfo_thread_tid_routes_to_tgid(void)
{
    /* Linux is permissive: rt_sigqueueinfo(tid_of_any_thread, ...)
     * succeeds and the signal lands in the thread group's pending set
     * (kill_pid_info routes through PIDTYPE_TGID). The contract under
     * test is that elfuse matches that routing: a worker thread tid is
     * accepted, and the queued signal becomes readable from the process
     * signalfd. A regression that scoped the syscall to "tgid only"
     * would surface here as ESRCH.
     */
    TEST("rt_sigqueueinfo tid routes to tgid");

    /* Block SIGRTMIN process-wide so the queued signal stays pending
     * for signalfd to read instead of terminating the process.
     */
    sigset_t block;
    sigemptyset(&block);
    sigaddset(&block, SIGRTMIN);
    sigprocmask(SIG_BLOCK, &block, NULL);

    worker_sync_t s;
    pthread_mutex_init(&s.mtx, NULL);
    pthread_cond_init(&s.ready_cv, NULL);
    pthread_cond_init(&s.go_cv, NULL);
    s.worker_tid = -1;
    s.ready = false;
    s.go = false;

    pthread_t th;
    if (pthread_create(&th, NULL, tid_worker, &s) != 0) {
        FAIL("pthread_create");
        return;
    }

    pthread_mutex_lock(&s.mtx);
    while (!s.ready)
        pthread_cond_wait(&s.ready_cv, &s.mtx);
    pid_t worker_tid = s.worker_tid;
    pthread_mutex_unlock(&s.mtx);

    if (worker_tid == getpid()) {
        pthread_mutex_lock(&s.mtx);
        s.go = true;
        pthread_cond_signal(&s.go_cv);
        pthread_mutex_unlock(&s.mtx);
        pthread_join(th, NULL);
        FAIL("worker tid equals process pid");
        return;
    }

    int sfd_fd = signalfd(-1, &block, SFD_NONBLOCK);
    if (sfd_fd < 0) {
        pthread_mutex_lock(&s.mtx);
        s.go = true;
        pthread_cond_signal(&s.go_cv);
        pthread_mutex_unlock(&s.mtx);
        pthread_join(th, NULL);
        FAIL("signalfd");
        return;
    }

    unsigned char info[128];
    build_kernel_siginfo(SIGRTMIN, -1, getpid(), getuid(), 0xBEEF, NULL, info);

    errno = 0;
    int ret = raw_rt_sigqueueinfo(worker_tid, SIGRTMIN, info);
    int err = errno;

    /* Drain any queued signal via signalfd before letting the worker
     * exit so the signal does not leak into pthread_join.
     */
    struct signalfd_siginfo rec;
    memset(&rec, 0, sizeof(rec));
    ssize_t got = -1;
    int got_err = 0;
    if (ret == 0) {
        errno = 0;
        got = read(sfd_fd, &rec, sizeof(rec));
        got_err = errno;
    }
    close(sfd_fd);

    pthread_mutex_lock(&s.mtx);
    s.go = true;
    pthread_cond_signal(&s.go_cv);
    pthread_mutex_unlock(&s.mtx);
    pthread_join(th, NULL);
    pthread_mutex_destroy(&s.mtx);
    pthread_cond_destroy(&s.ready_cv);
    pthread_cond_destroy(&s.go_cv);

    if (ret != 0) {
        printf("FAIL: worker tid %d: ret=%d errno=%d (expected 0)\n",
               (int) worker_tid, ret, err);
        fails++;
        return;
    }
    if (got != (ssize_t) sizeof(rec) || rec.ssi_signo != (uint32_t) SIGRTMIN ||
        rec.ssi_int != 0xBEEF) {
        printf("FAIL: signalfd read got=%zd errno=%d signo=%u int=0x%x\n", got,
               got_err, rec.ssi_signo, rec.ssi_int);
        fails++;
        return;
    }
    PASS();
}

int main(void)
{
    printf("test-signalfd-hardening: signalfd read semantics audit\n");

    test_rt_multiplicity();
    test_standard_coalesces();
    test_sigrtmax_reachable();
    test_ssi_ptr_roundtrip();
    test_sender_metadata();
    test_mask_filters_only();
    test_sigqueue_libc_path();
    test_sigqueue_standard_metadata();
    test_partial_fault_returns_partial_bytes();
    test_rt_sigqueueinfo_bad_pointer_efault();
    test_rt_sigqueueinfo_rejects_foreign_pid();
    test_rt_sigqueueinfo_thread_tid_routes_to_tgid();

    SUMMARY("test-signalfd-hardening");
    return fails > 0 ? 1 : 0;
}
