#pragma once

#include <unistd.h>
#include <fcntl.h>
#include <sys_compat/muplar_errno.h>

static inline int missing_syncfs(int fd) {
#if defined(__APPLE__)
    /* Best approximation of syncfs() on macOS */
#ifdef F_FULLFSYNC
    if (fcntl(fd, F_FULLFSYNC) == 0)
        return 0;
#endif
    return fsync(fd);
#else
    return syncfs(fd);
#endif
}

#ifndef HAVE_SYNCFS
#define syncfs missing_syncfs
#endif
