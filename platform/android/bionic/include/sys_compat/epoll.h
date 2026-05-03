// compat/epoll.h
#pragma once

#ifdef __APPLE__

#include <sys/types.h>
#include <sys/event.h>
#include <sys/time.h>
#include <unistd.h>
#include <sys_compat/muplar_errno.h>
#include <stdlib.h>
#include <fcntl.h>

#define EPOLLIN  0x001
#define EPOLLOUT 0x004
#define EPOLLERR 0x008
#define EPOLLHUP 0x010
#define EPOLL_CLOEXEC 0
#define EPOLL_CTL_ADD 1
#define EPOLL_CTL_MOD 2
#define EPOLL_CTL_DEL 3
#define EPOLLONESHOT 0x10000000
#define EPOLLIN 0x001

#ifndef EPOLLPRI
#define EPOLLPRI 0
#endif

#ifndef EPOLLRDHUP
#define EPOLLRDHUP 0
#endif

#ifndef EPOLLET
#define EPOLLET 0
#endif

typedef union epoll_data {
    void *ptr;
    int fd;
    uint32_t u32;
    uint64_t u64;
} epoll_data_t;

typedef struct epoll_event {
    uint32_t events;   // EPOLLIN, EPOLLOUT, etc.
    int fd;            // file descriptor
    epoll_data_t data;
} epoll_event;

static inline int epoll_create(int size) {
    (void)size; // ignored
    int kq = kqueue();
    return kq < 0 ? -1 : kq;
}

static inline int epoll_ctl(int epfd, int op, int fd, struct epoll_event *event) {
    struct kevent kev;
    int flags = 0;

    switch(op) {
        case 1: // EPOLL_CTL_ADD
            flags = EV_ADD | EV_ENABLE;
            break;
        case 2: // EPOLL_CTL_MOD
            flags = EV_ADD | EV_ENABLE;
            break;
        case 3: // EPOLL_CTL_DEL
            EV_SET(&kev, fd, EVFILT_READ, EV_DELETE, 0, 0, NULL);
            kevent(epfd, &kev, 1, NULL, 0, NULL);
            EV_SET(&kev, fd, EVFILT_WRITE, EV_DELETE, 0, 0, NULL);
            return kevent(epfd, &kev, 1, NULL, 0, NULL);
        default:
            errno = EINVAL;
            return -1;
    }

    if(event->events & EPOLLIN)
        EV_SET(&kev, fd, EVFILT_READ, flags, 0, 0, NULL);
    else if(event->events & EPOLLOUT)
        EV_SET(&kev, fd, EVFILT_WRITE, flags, 0, 0, NULL);
    else
        return -1;

    return kevent(epfd, &kev, 1, NULL, 0, NULL);
}

static inline int epoll_wait(int epfd, struct epoll_event *events, int maxevents, int timeout) {
    struct kevent kev[maxevents];
    struct timespec ts;
    struct timespec *tsp = NULL;

    if(timeout >= 0) {
        ts.tv_sec = timeout / 1000;
        ts.tv_nsec = (timeout % 1000) * 1000000;
        tsp = &ts;
    }

    int n = kevent(epfd, NULL, 0, kev, maxevents, tsp);
    if(n < 0) return n;

    for(int i = 0; i < n; i++) {
        events[i].fd = (int)kev[i].ident;
        events[i].events = 0;
        if(kev[i].filter == EVFILT_READ) events[i].events |= EPOLLIN;
        if(kev[i].filter == EVFILT_WRITE) events[i].events |= EPOLLOUT;
        if(kev[i].flags & EV_ERROR) events[i].events |= EPOLLERR;
        if(kev[i].flags & EV_EOF)   events[i].events |= EPOLLHUP;
    }

    return n;
}

static inline int epoll_create1(int flags) {
    int fd = epoll_create(1);
    if (fd < 0) return -1;
    if (flags & EPOLL_CLOEXEC)
        fcntl(fd, F_SETFD, FD_CLOEXEC);
    return fd;
}

#endif // __APPLE__
