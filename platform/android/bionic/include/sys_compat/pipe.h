#ifndef SYS_COMPAT_PIPE_H
#define SYS_COMPAT_PIPE_H

#include <unistd.h>
#include <fcntl.h>

static inline int pipe2(int fd[2], int flags) {
    int res = pipe(fd);
    if (res == 0 && (flags & O_CLOEXEC)) {
        fcntl(fd[0], F_SETFD, FD_CLOEXEC);
        fcntl(fd[1], F_SETFD, FD_CLOEXEC);
    }
    return res;
}

#endif /* SYS_COMPAT_PIPE_H */
