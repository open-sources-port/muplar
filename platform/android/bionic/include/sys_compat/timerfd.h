// timerfd_compat.h
#pragma once

#ifdef __APPLE__
#include <sys/event.h>
#include <unistd.h>
#include <sys_compat/muplar_errno.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>
#include <stdio.h>

typedef int timerfd_t;

#define TFD_NONBLOCK 0x1
#define TFD_CLOEXEC  0x2
#define TFD_TIMER_ABSTIME 1

struct itimerspec {
    struct timespec it_interval;
    struct timespec it_value;
};

static inline timerfd_t timerfd_create(int clockid, int flags) {
    // Use kqueue fd as timerfd
    int kq = kqueue();
    if (kq == -1) return -1;
    return kq;
}

static inline int timerfd_settime(timerfd_t fd, int flags,
                                  const struct itimerspec *new_value,
                                  struct itimerspec *old_value) {
    (void)flags;
    if (!new_value) {
        errno = EINVAL;
        return -1;
    }

    struct kevent kev;
    EV_SET(&kev, 1, EVFILT_TIMER, EV_ADD | EV_ENABLE | EV_ONESHOT,
           0, new_value->it_value.tv_sec * 1000, NULL);
    return kevent(fd, &kev, 1, NULL, 0, NULL);
}

static inline int timerfd_read(timerfd_t fd, uint64_t *val) {
    uint64_t tmp;
    ssize_t s = read(fd, &tmp, sizeof(tmp));
    if (s != sizeof(tmp)) return -1;
    *val = tmp;
    return 0;
}

#endif
