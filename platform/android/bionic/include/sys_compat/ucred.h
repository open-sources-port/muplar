/* SPDX-License-Identifier: LGPL-2.1-or-later */
#pragma once

/*
 * macOS implementation of Linux ucred / SCM_CREDENTIALS APIs.
 *
 * This header is intended for ports that ONLY run on macOS.
 * It intentionally avoids any __linux__ conditionals.
 */

#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>
#include <sys_compat/muplar_errno.h>

/* ---------------------------------------------------------------------
 * Linux-compatible definitions
 * --------------------------------------------------------------------- */

/* Linux struct ucred */
struct ucred {
        pid_t pid;
        uid_t uid;
        gid_t gid;
};

/*
 * Linux socket options – not supported on macOS.
 * Defined to harmless values so code compiles.
 */
#define SO_PASSCRED     0
#define SCM_CREDENTIALS 0

/* ---------------------------------------------------------------------
 * Credential helper
 * --------------------------------------------------------------------- */

static inline int ucred_from_peer(int fd, struct ucred *u) {
    uid_t uid;
    gid_t gid;

    if (!u) {
        errno = EINVAL;
        return -1;
    }

    if (getpeereid(fd, &uid, &gid) < 0)
        return -1;

    u->uid = uid;
    u->gid = gid;

    /* macOS extension: peer PID */
    pid_t pid = 0;

    #if defined(__APPLE__) && defined(__has_builtin)
    #  if __has_builtin(getpeerpid_np)
        (void) getpeerpid_np(fd, &pid);
    #  endif
    #endif

    u->pid = pid;

    return 0;
}

/* ---------------------------------------------------------------------
 * SCM_CREDENTIALS compatibility
 * --------------------------------------------------------------------- */

/*
 * systemd expects SCM_CREDENTIALS to be retrieved via recvmsg().
 * macOS does not support this, so provide a helper macro that
 * returns NULL, forcing callers to fall back to ucred_from_peer().
 */
// #define CMSG_FIND_DATA(msg, level, type, ctype) NULL
/* Type-safe, dereferencing version of cmsg_find() */
#define CMSG_FIND_DATA(mh, level, type, ctype) \
        ({                                                            \
                struct cmsghdr *_found;                               \
                _found = cmsg_find(mh, level, type, CMSG_LEN(sizeof(ctype))); \
                (ctype*) (_found ? CMSG_DATA(_found) : NULL);         \
        })
