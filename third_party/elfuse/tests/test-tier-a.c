/* Tier A compatibility tests
 *
 * Copyright 2026 elfuse contributors
 * SPDX-License-Identifier: Apache-2.0
 *
 * Tests: SysV message queues, memory policy stubs, mlockall stubs,
 * prctl new options, /proc/self/task, PAC feature probe.
 */

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/auxv.h>
#include <sys/ipc.h>
#include <sys/mman.h>
#include <sys/msg.h>
#include <sys/prctl.h>
#include <unistd.h>

#include "test-harness.h"

int passes = 0, fails = 0;

/* SysV message queues. */

struct test_msgbuf {
    long mtype;
    char mtext[64];
};

static void test_msgget_create(void)
{
    TEST("msgget IPC_PRIVATE");
    int msqid = msgget(IPC_PRIVATE, IPC_CREAT | 0600);
    if (msqid < 0) {
        FAIL("msgget");
        return;
    }
    PASS();
    msgctl(msqid, IPC_RMID, NULL);
}

static void test_msgsnd_msgrcv(void)
{
    TEST("msgsnd + msgrcv round-trip");
    int msqid = msgget(IPC_PRIVATE, IPC_CREAT | 0600);
    if (msqid < 0) {
        FAIL("msgget");
        return;
    }

    struct test_msgbuf snd;
    snd.mtype = 42;
    memcpy(snd.mtext, "hello-msg", 10);

    if (msgsnd(msqid, &snd, 10, 0) < 0) {
        FAIL("msgsnd");
        msgctl(msqid, IPC_RMID, NULL);
        return;
    }

    struct test_msgbuf rcv;
    memset(&rcv, 0, sizeof(rcv));
    ssize_t n = msgrcv(msqid, &rcv, sizeof(rcv.mtext), 42, 0);
    if (n < 0) {
        FAIL("msgrcv");
        msgctl(msqid, IPC_RMID, NULL);
        return;
    }

    if (n != 10 || rcv.mtype != 42 || memcmp(rcv.mtext, "hello-msg", 10) != 0)
        FAIL("data mismatch");
    else
        PASS();

    msgctl(msqid, IPC_RMID, NULL);
}

static void test_msgctl_stat(void)
{
    TEST("msgctl IPC_STAT");
    int msqid = msgget(IPC_PRIVATE, IPC_CREAT | 0600);
    if (msqid < 0) {
        FAIL("msgget");
        return;
    }

    struct test_msgbuf snd = {.mtype = 1};
    memcpy(snd.mtext, "test", 5);
    msgsnd(msqid, &snd, 5, 0);

    struct msqid_ds ds;
    if (msgctl(msqid, IPC_STAT, &ds) < 0) {
        FAIL("msgctl IPC_STAT");
        msgctl(msqid, IPC_RMID, NULL);
        return;
    }

    if (ds.msg_qnum != 1)
        FAIL("msg_qnum != 1");
    else
        PASS();

    msgctl(msqid, IPC_RMID, NULL);
}

static void test_msgrcv_nowait(void)
{
    TEST("msgrcv IPC_NOWAIT empty");
    int msqid = msgget(IPC_PRIVATE, IPC_CREAT | 0600);
    if (msqid < 0) {
        FAIL("msgget");
        return;
    }

    struct test_msgbuf rcv;
    EXPECT_ERRNO(msgrcv(msqid, &rcv, sizeof(rcv.mtext), 0, IPC_NOWAIT), ENOMSG,
                 "expected ENOMSG");

    msgctl(msqid, IPC_RMID, NULL);
}

static void test_msgrcv_except_unsupported(void)
{
    TEST("msgrcv MSG_EXCEPT unsupported");
    int msqid = msgget(IPC_PRIVATE, IPC_CREAT | 0600);
    if (msqid < 0) {
        FAIL("msgget");
        return;
    }

    struct test_msgbuf snd = {.mtype = 1};
    memcpy(snd.mtext, "x", 2);
    if (msgsnd(msqid, &snd, 2, 0) < 0) {
        FAIL("msgsnd");
        msgctl(msqid, IPC_RMID, NULL);
        return;
    }

    errno = 0;
    struct test_msgbuf rcv;
    EXPECT_ERRNO(msgrcv(msqid, &rcv, sizeof(rcv.mtext), 1, MSG_EXCEPT), ENOSYS,
                 "expected ENOSYS for MSG_EXCEPT");

    msgctl(msqid, IPC_RMID, NULL);
}

static void test_msgctl_rmid(void)
{
    TEST("msgctl IPC_RMID");
    int msqid = msgget(IPC_PRIVATE, IPC_CREAT | 0600);
    if (msqid < 0) {
        FAIL("msgget");
        return;
    }
    if (msgctl(msqid, IPC_RMID, NULL) < 0)
        FAIL("msgctl IPC_RMID");
    else
        PASS();
}

/* Memory policy stubs. */

/* get_mempolicy(2) and set_mempolicy(2) are Linux-specific.
 * Use raw syscall since glibc may not wrap them.
 */
#ifndef __NR_get_mempolicy
#define __NR_get_mempolicy 239
#endif
#ifndef __NR_set_mempolicy
#define __NR_set_mempolicy 238
#endif

static void test_get_mempolicy(void)
{
    TEST("get_mempolicy stub");
    int policy = -1;
    long ret = syscall(__NR_get_mempolicy, &policy, NULL, 0, NULL, 0);
    if (ret < 0) {
        FAIL("get_mempolicy returned error");
        return;
    }
    if (policy != 0) /* MPOL_DEFAULT = 0 */
        FAIL("policy != MPOL_DEFAULT");
    else
        PASS();
}

static void test_set_mempolicy(void)
{
    TEST("set_mempolicy stub");
    long ret = syscall(__NR_set_mempolicy, 0 /* MPOL_DEFAULT */, NULL, 0);
    if (ret < 0)
        FAIL("set_mempolicy returned error");
    else
        PASS();
}

/* mlockall / munlockall stubs. */

#ifndef MCL_CURRENT
#define MCL_CURRENT 1
#endif
#ifndef MCL_FUTURE
#define MCL_FUTURE 2
#endif

#ifndef __NR_mlockall
#define __NR_mlockall 230
#endif
#ifndef __NR_munlockall
#define __NR_munlockall 231
#endif

static void test_mlockall(void)
{
    TEST("mlockall stub");
    long ret = syscall(__NR_mlockall, MCL_CURRENT | MCL_FUTURE);
    if (ret < 0)
        FAIL("mlockall returned error");
    else
        PASS();
}

static void test_munlockall(void)
{
    TEST("munlockall stub");
    long ret = syscall(__NR_munlockall);
    if (ret < 0)
        FAIL("munlockall returned error");
    else
        PASS();
}

/* prctl new options. */

#ifndef PR_SET_CHILD_SUBREAPER
#define PR_SET_CHILD_SUBREAPER 36
#endif
#ifndef PR_GET_CHILD_SUBREAPER
#define PR_GET_CHILD_SUBREAPER 37
#endif
#ifndef PR_CAPBSET_READ
#define PR_CAPBSET_READ 23
#endif

/* PR_SET_VMA is Linux 5.17+ */
#ifndef PR_SET_VMA
#define PR_SET_VMA 0x53564d41
#define PR_SET_VMA_ANON_NAME 0
#endif

static void test_prctl_child_subreaper(void)
{
    TEST("prctl PR_SET_CHILD_SUBREAPER");
    if (prctl(PR_SET_CHILD_SUBREAPER, 1, 0, 0, 0) < 0) {
        FAIL("PR_SET_CHILD_SUBREAPER");
        return;
    }

    int val = -1;
    if (prctl(PR_GET_CHILD_SUBREAPER, (unsigned long) &val, 0, 0, 0) < 0) {
        FAIL("PR_GET_CHILD_SUBREAPER");
        return;
    }

    if (val != 1)
        FAIL("subreaper value != 1");
    else
        PASS();
}

static void test_prctl_capbset_read(void)
{
    TEST("prctl PR_CAPBSET_READ");
    /* CAP_SYS_ADMIN = 21 */
    int ret = prctl(PR_CAPBSET_READ, 21, 0, 0, 0);
    if (ret < 0) {
        FAIL("PR_CAPBSET_READ returned error");
        return;
    }
    if (ret != 1)
        FAIL("expected capability present (1)");
    else
        PASS();
}

static void test_prctl_capbset_read_invalid(void)
{
    TEST("prctl PR_CAPBSET_READ invalid");
    EXPECT_ERRNO(prctl(PR_CAPBSET_READ, 999, 0, 0, 0), EINVAL,
                 "expected EINVAL for cap 999");
}

static void test_prctl_capbset_read_boundary(void)
{
    TEST("prctl PR_CAPBSET_READ boundary");
    int ret = prctl(PR_CAPBSET_READ, 40, 0, 0, 0);
    if (ret != 1) {
        FAIL("expected CAP_LAST_CAP present");
        return;
    }

    errno = 0;
    EXPECT_ERRNO(prctl(PR_CAPBSET_READ, 41, 0, 0, 0), EINVAL,
                 "expected EINVAL above CAP_LAST_CAP");
}

static void test_prctl_set_vma_anon_name(void)
{
    TEST("prctl PR_SET_VMA_ANON_NAME");
    void *p = mmap(NULL, 4096, PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (p == MAP_FAILED) {
        FAIL("mmap");
        return;
    }

    int ret = prctl(PR_SET_VMA, PR_SET_VMA_ANON_NAME, (unsigned long) p, 4096,
                    (unsigned long) "test-region");
    if (ret < 0)
        FAIL("PR_SET_VMA_ANON_NAME");
    else
        PASS();

    munmap(p, 4096);
}

/* /proc/self/task. */

static void test_proc_self_task_open(void)
{
    TEST("/proc/self/task openable");
    int fd = open("/proc/self/task", O_RDONLY | O_DIRECTORY);
    if (fd < 0)
        FAIL("open /proc/self/task");
    else {
        PASS();
        close(fd);
    }
}

static void test_proc_self_task_tid_stat(void)
{
    TEST("/proc/self/task/<tid>/stat");
    pid_t tid = getpid(); /* main thread TID == PID */
    char path[128];
    snprintf(path, sizeof(path), "/proc/self/task/%d/stat", tid);
    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        FAIL("open task stat");
        return;
    }

    char buf[512];
    ssize_t n = read(fd, buf, sizeof(buf) - 1);
    close(fd);
    if (n <= 0) {
        FAIL("read task stat");
        return;
    }
    buf[n] = '\0';

    /* First field should be the TID */
    long parsed_tid = strtol(buf, NULL, 10);
    EXPECT_TRUE(parsed_tid == tid, "TID mismatch in stat");
}

static void test_proc_self_task_tid_status(void)
{
    TEST("/proc/self/task/<tid>/status");
    pid_t tid = getpid();
    char path[128];
    snprintf(path, sizeof(path), "/proc/self/task/%d/status", tid);
    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        FAIL("open task status");
        return;
    }

    char buf[1024];
    ssize_t n = read(fd, buf, sizeof(buf) - 1);
    close(fd);
    if (n <= 0) {
        FAIL("read task status");
        return;
    }
    buf[n] = '\0';

    EXPECT_TRUE(strstr(buf, "Name:") && strstr(buf, "Pid:"),
                "missing fields in task status");
}

static void test_proc_self_task_openat_traversal(void)
{
    TEST("/proc/self/task openat traversal");
    int taskfd = open("/proc/self/task", O_RDONLY | O_DIRECTORY);
    if (taskfd < 0) {
        FAIL("open task dir");
        return;
    }

    char tidbuf[32];
    snprintf(tidbuf, sizeof(tidbuf), "%d", getpid());
    int tidfd = openat(taskfd, tidbuf, O_RDONLY | O_DIRECTORY);
    if (tidfd < 0) {
        close(taskfd);
        FAIL("openat tid dir");
        return;
    }

    int statusfd = openat(tidfd, "status", O_RDONLY);
    if (statusfd < 0) {
        close(tidfd);
        close(taskfd);
        FAIL("openat status");
        return;
    }

    char buf[1024];
    ssize_t n = read(statusfd, buf, sizeof(buf) - 1);
    close(statusfd);
    close(tidfd);
    close(taskfd);
    if (n <= 0) {
        FAIL("read status");
        return;
    }
    buf[n] = '\0';

    EXPECT_TRUE(strstr(buf, "Pid:") && strstr(buf, "Threads:"),
                "missing fields in openat status");
}

/* PAC feature probe. */

static void test_pac_hwcap(void)
{
    TEST("PAC feature (getauxval)");
    unsigned long hwcap = getauxval(16 /* AT_HWCAP */);

    /* HWCAP_PACA = 1<<30, HWCAP_PACG = 1<<31 */
    if ((hwcap & ((1UL << 30) | (1UL << 31))) == ((1UL << 30) | (1UL << 31)))
        PASS();
    else
        FAIL("PACA/PACG not set in HWCAP");
}

/* AT_SECURE auxv probe. Bionic aborts when AT_SECURE is missing from the
 * auxiliary vector. elfuse never elevates privileges, so the value is 0.
 * getauxval() also returns 0 when an entry is not present, so distinguish
 * the two by walking /proc/self/auxv and looking for the AT_SECURE key.
 *
 * Read /proc/self/auxv with a loop: procfs is not required to return the
 * whole file in one read, and EINTR must be retried. AT_NULL must appear
 * in the accumulated buffer or the test cannot conclude.
 */
static void test_at_secure_present(void)
{
    TEST("AT_SECURE present and zero");

    int fd = open("/proc/self/auxv", O_RDONLY);
    if (fd < 0) {
        FAIL("cannot open /proc/self/auxv");
        return;
    }
    uint64_t buf[128];
    size_t total = 0;
    for (;;) {
        ssize_t n = read(fd, (char *) buf + total, sizeof(buf) - total);
        if (n < 0) {
            if (errno == EINTR)
                continue;
            close(fd);
            FAIL("read /proc/self/auxv failed");
            return;
        }
        if (n == 0)
            break;
        total += (size_t) n;
        if (total >= sizeof(buf))
            break;
    }
    close(fd);
    if (total == 0 || (total & 7) != 0) {
        FAIL("auxv read returned no bytes or misaligned length");
        return;
    }

    int found = 0, terminated = 0;
    uint64_t value = 0;
    for (size_t i = 0; i + 1 < total / 8; i += 2) {
        if (buf[i] == 0 /* AT_NULL */) {
            terminated = 1;
            break;
        }
        if (buf[i] == 23 /* AT_SECURE */) {
            found = 1;
            value = buf[i + 1];
            break;
        }
    }

    if (found) {
        if (value != 0)
            FAIL("AT_SECURE present but non-zero");
        else
            PASS();
    } else if (!terminated) {
        FAIL("AT_NULL not reached in auxv buffer");
    } else {
        FAIL("AT_SECURE missing from /proc/self/auxv");
    }
}

int main(void)
{
    printf("test-tier-a: Tier A compatibility tests\n\n");

    /* SysV message queues */
    test_msgget_create();
    test_msgsnd_msgrcv();
    test_msgctl_stat();
    test_msgrcv_nowait();
    test_msgrcv_except_unsupported();
    test_msgctl_rmid();
    /* Memory policy stubs */
    test_get_mempolicy();
    test_set_mempolicy();

    /* mlockall stubs */
    test_mlockall();
    test_munlockall();

    /* prctl extensions */
    test_prctl_child_subreaper();
    test_prctl_capbset_read();
    test_prctl_capbset_read_invalid();
    test_prctl_capbset_read_boundary();
    test_prctl_set_vma_anon_name();

    /* /proc/self/task */
    test_proc_self_task_open();
    test_proc_self_task_tid_stat();
    test_proc_self_task_tid_status();
    test_proc_self_task_openat_traversal();

    /* PAC feature */
    test_pac_hwcap();

    /* AT_SECURE auxv entry */
    test_at_secure_present();

    SUMMARY("test-tier-a");
    return fails > 0 ? 1 : 0;
}
