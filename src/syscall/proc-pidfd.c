/* pidfd helpers
 *
 * Copyright 2026 elfuse contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/event.h>
#include <unistd.h>

#include "utils.h"

#include "syscall/internal.h"
#include "syscall/proc.h"
#include "syscall/proc-pidfd.h"
#include "syscall/signal.h"

#define PIDFD_TABLE_SIZE 32

typedef struct {
    bool active;
    int guest_fd;
    int64_t guest_pid;
    int write_end;
} pidfd_entry_t;

static pidfd_entry_t pidfd_table[PIDFD_TABLE_SIZE];
static pthread_mutex_t pidfd_lock = PTHREAD_MUTEX_INITIALIZER;

static pidfd_entry_t *pidfd_find_free_entry(void)
{
    for (int i = 0; i < PIDFD_TABLE_SIZE; i++) {
        if (!pidfd_table[i].active)
            return &pidfd_table[i];
    }
    return NULL;
}

static pidfd_entry_t *pidfd_find_guest_fd_entry(int guest_fd)
{
    for (int i = 0; i < PIDFD_TABLE_SIZE; i++) {
        if (pidfd_table[i].active && pidfd_table[i].guest_fd == guest_fd)
            return &pidfd_table[i];
    }
    return NULL;
}

static void pidfd_cleanup(int guest_fd)
{
    pthread_mutex_lock(&pidfd_lock);
    pidfd_entry_t *entry = pidfd_find_guest_fd_entry(guest_fd);
    if (entry) {
        if (entry->write_end >= 0)
            close(entry->write_end);
        entry->write_end = -1;
        entry->active = false;
    }
    pthread_mutex_unlock(&pidfd_lock);
}

static void *pidfd_monitor_thread(void *arg)
{
    int64_t *pids = (int64_t *) arg;
    int64_t gpid = pids[0];
    pid_t hpid = (pid_t) pids[1];
    free(arg);

    if (kill(hpid, 0) < 0 && errno == ESRCH) {
        proc_pidfd_notify_exit(gpid);
        return NULL;
    }

    int kq = kqueue();
    if (kq < 0)
        return NULL;

    struct kevent ev;
    EV_SET(&ev, hpid, EVFILT_PROC, EV_ADD | EV_ONESHOT, NOTE_EXIT, 0, NULL);
    if (kevent(kq, &ev, 1, NULL, 0, NULL) < 0) {
        close(kq);
        proc_pidfd_notify_exit(gpid);
        return NULL;
    }

    struct kevent out;
    int n = kevent(kq, NULL, 0, &out, 1, NULL);
    close(kq);

    if (n > 0 && out.filter == EVFILT_PROC)
        proc_pidfd_notify_exit(gpid);

    return NULL;
}

int pidfd_create(guest_t *g, int64_t target_pid)
{
    (void) g;
    int pfd[2];
    if (pipe(pfd) < 0)
        return -LINUX_EMFILE;
    if (fd_set_cloexec(pfd[0]) < 0 || fd_set_cloexec(pfd[1]) < 0) {
        close(pfd[0]);
        close(pfd[1]);
        return linux_errno();
    }

    int gfd = fd_alloc(FD_PIDFD, pfd[0], pidfd_cleanup);
    if (gfd < 0) {
        close(pfd[0]);
        close(pfd[1]);
        return -LINUX_EMFILE;
    }

    pthread_mutex_lock(&pidfd_lock);
    pidfd_entry_t *entry = pidfd_find_free_entry();
    if (!entry) {
        pthread_mutex_unlock(&pidfd_lock);
        fd_entry_t snap;
        if (fd_snapshot_and_close(gfd, &snap))
            fd_cleanup_entry(gfd, &snap);
        close(pfd[1]);
        return -LINUX_EMFILE;
    }

    entry->active = true;
    entry->guest_fd = gfd;
    entry->guest_pid = target_pid;
    entry->write_end = pfd[1];
    pthread_mutex_unlock(&pidfd_lock);

    pid_t host_pid = proc_guest_to_host_pid(target_pid);
    if (host_pid > 0) {
        bool monitor_ok = false;
        int64_t *ctx = malloc(2 * sizeof(int64_t));
        if (ctx) {
            ctx[0] = target_pid;
            ctx[1] = (int64_t) host_pid;
            pthread_t thr;
            pthread_attr_t attr;
            if (pthread_attr_init(&attr) == 0) {
                if (pthread_attr_setdetachstate(&attr,
                                                PTHREAD_CREATE_DETACHED) == 0 &&
                    pthread_create(&thr, &attr, pidfd_monitor_thread, ctx) ==
                        0) {
                    monitor_ok = true;
                } else {
                    free(ctx);
                }
                pthread_attr_destroy(&attr);
            } else {
                free(ctx);
            }
        }
        if (!monitor_ok)
            proc_pidfd_notify_exit(target_pid);
    }

    return gfd;
}

void proc_pidfd_notify_exit(int64_t exited_pid)
{
    pthread_mutex_lock(&pidfd_lock);
    for (int i = 0; i < PIDFD_TABLE_SIZE; i++) {
        if (pidfd_table[i].active && pidfd_table[i].guest_pid == exited_pid &&
            pidfd_table[i].write_end >= 0) {
            uint8_t byte = 0;
            (void) write(pidfd_table[i].write_end, &byte, 1);
            close(pidfd_table[i].write_end);
            pidfd_table[i].write_end = -1;
        }
    }
    pthread_mutex_unlock(&pidfd_lock);
}

int64_t proc_pidfd_lookup_pid(int guest_fd)
{
    pthread_mutex_lock(&pidfd_lock);
    pidfd_entry_t *entry = pidfd_find_guest_fd_entry(guest_fd);
    if (entry) {
        int64_t pid = entry->guest_pid;
        pthread_mutex_unlock(&pidfd_lock);
        return pid;
    }
    pthread_mutex_unlock(&pidfd_lock);
    return -1;
}

int64_t sys_pidfd_open(guest_t *g, int64_t pid, unsigned int flags)
{
    if (flags != 0)
        return -LINUX_EINVAL;

    if (pid == proc_get_pid())
        return pidfd_create(g, pid);

    if (proc_guest_to_host_pid(pid) > 0)
        return pidfd_create(g, pid);

    return -LINUX_ESRCH;
}

int64_t sys_pidfd_send_signal(guest_t *g,
                              int pidfd,
                              int sig,
                              uint64_t info_gva,
                              unsigned int flags)
{
    (void) g;
    (void) info_gva;

    if (flags != 0)
        return -LINUX_EINVAL;

    int64_t pid = proc_pidfd_lookup_pid(pidfd);
    if (pid < 0)
        return -LINUX_EBADF;

    if (sig < 0 || sig > 64)
        return -LINUX_EINVAL;

    if (pid == proc_get_pid()) {
        if (sig != 0)
            signal_queue(sig);
        return 0;
    }

    pid_t host_pid = proc_guest_to_host_pid(pid);
    if (host_pid > 0) {
        if (sig == 0) {
            if (kill(host_pid, 0) < 0)
                return -LINUX_ESRCH;
            return 0;
        }
        if (proc_send_guest_signal(host_pid, sig) < 0)
            return linux_errno();
        return 0;
    }

    return -LINUX_ESRCH;
}
