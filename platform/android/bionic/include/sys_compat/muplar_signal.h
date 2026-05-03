#pragma once

#ifdef __APPLE__
#include <signal.h>
#include <string.h>
#include <sys_compat/muplar_errno.h>
#include <time.h>
#include <sys/select.h>

/* Linux signals that macOS does not define */
#ifndef SIGPWR
#define SIGPWR 0
#endif

#ifndef SIGRTMIN
#define SIGRTMIN 0
#endif

#ifndef SIGRTMAX
#define SIGRTMAX 0
#endif

static inline int sigisemptyset(const sigset_t *set) {
    sigset_t empty;
    sigemptyset(&empty);
    return memcmp(set, &empty, sizeof(sigset_t)) == 0;
}

static inline int sigtimedwait(const sigset_t *mask,
                siginfo_t *info,
                const struct timespec *timeout) {

        int sig, r;

        /* Timeout handling */
        if (timeout) {
                struct timeval tv = {
                        .tv_sec = timeout->tv_sec,
                        .tv_usec = timeout->tv_nsec / 1000
                };

                r = select(0, NULL, NULL, NULL, &tv);
                if (r == 0) {
                        errno = EAGAIN;
                        return -1;
                }
                if (r < 0)
                        return -1;
        }

        r = sigwait(mask, &sig);
        if (r != 0) {
                errno = r;
                return -1;
        }

        /* macOS has no siginfo for sigwait */
        if (info)
                memset(info, 0, sizeof(*info));

        return sig;
}

#endif
