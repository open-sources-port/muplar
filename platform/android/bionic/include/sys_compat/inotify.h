// compat/inotify.h
#pragma once

#ifdef __APPLE__

#include <sys/types.h>
#include <sys/event.h>
#include <sys/time.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys_compat/muplar_errno.h>
#include <string.h>

/* inotify event masks (Linux-only) */
#define IN_ACCESS        0x00000001
#define IN_MODIFY        0x00000002
#define IN_ATTRIB        0x00000004
#define IN_CLOSE_WRITE   0x00000008
#define IN_CLOSE_NOWRITE 0x00000010
#define IN_OPEN          0x00000020
#define IN_MOVED_FROM    0x00000040
#define IN_MOVED_TO      0x00000080
#define IN_CREATE        0x00000100
#define IN_DELETE        0x00000200
#define IN_DELETE_SELF   0x00000400
#define IN_MOVE_SELF     0x00000800

/* Missing on macOS */
#define IN_UNMOUNT       0x00002000
#define IN_Q_OVERFLOW    0x00004000
#define IN_IGNORED       0x00008000
#define IN_ONLYDIR       0x01000000

#ifndef IN_NONBLOCK
#define IN_NONBLOCK 0
#endif

#ifndef IN_CLOEXEC
#define IN_CLOEXEC 0
#endif

#ifndef IN_DONT_FOLLOW
#define IN_DONT_FOLLOW 0
#endif

#ifndef CMSG_SPACE
// simplified version
#define CMSG_SPACE(len) ((len) + sizeof(struct cmsghdr))
#endif

#ifndef IN_EXCL_UNLINK
#define IN_EXCL_UNLINK 0
#endif

#ifndef IN_ONESHOT
#define IN_ONESHOT 0
#endif

#ifndef IN_MASK_ADD
#define IN_MASK_ADD 0
#endif

#ifndef IN_ALL_EVENTS
#define IN_ALL_EVENTS 0
#endif

#define IN_ISDIR 0

typedef int inotify_fd_t;  // the kqueue fd

struct inotify_event {
    int wd;             // watch descriptor
    uint32_t mask;      // event type
    char name[256];     // optional name
    uint32_t len; // Length
};

// Event masks
#define IN_ACCESS        0x00000001
#define IN_MODIFY        0x00000002
#define IN_ATTRIB        0x00000004
#define IN_CLOSE_WRITE   0x00000008
#define IN_CLOSE_NOWRITE 0x00000010
#define IN_OPEN          0x00000020
#define IN_MOVED_FROM    0x00000040
#define IN_MOVED_TO      0x00000080
#define IN_CREATE        0x00000100
#define IN_DELETE        0x00000200
#define IN_DELETE_SELF   0x00000400
#define IN_MOVE_SELF     0x00000800

static inline int inotify_init(void) {
    int kq = kqueue();
    return kq < 0 ? -1 : kq;
}

static inline int inotify_init1(int flags) {
    (void)flags; // ignore flags
    return inotify_init();
}

static inline int inotify_add_watch(int kq, const char *pathname, uint32_t mask) {
    int fd = open(pathname, O_EVTONLY);
    if(fd < 0) return -1;

    struct kevent kev;
    EV_SET(&kev, fd, EVFILT_VNODE,
           EV_ADD | EV_ENABLE | EV_CLEAR,
           NOTE_WRITE | NOTE_DELETE | NOTE_EXTEND | NOTE_ATTRIB | NOTE_RENAME,
           0, NULL);

    if(kevent(kq, &kev, 1, NULL, 0, NULL) < 0) {
        close(fd);
        return -1;
    }

    return fd; // fd as watch descriptor
}

static inline int inotify_rm_watch(int kq, int wd) {
    struct kevent kev;
    EV_SET(&kev, wd, EVFILT_VNODE, EV_DELETE, 0, 0, NULL);
    kevent(kq, &kev, 1, NULL, 0, NULL);
    close(wd);
    return 0;
}

#endif // __APPLE__
