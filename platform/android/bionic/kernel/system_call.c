#include <unistd.h>
#include <fcntl.h>
#include <sys_compat/muplar_errno.h>
#include <sys_compat/missing_syscall.h>

#ifdef __APPLE__  // macOS / BSD fallback
int dup3(int oldfd, int newfd, int flags) {
    if (oldfd == newfd) {
        errno = EINVAL;
        return -1;
    }

    close(newfd);

    int r = dup2(oldfd, newfd);
    if (r < 0)
        return r;

    if (flags & O_CLOEXEC) {
        int fl = fcntl(newfd, F_GETFD);
        if (fl == -1)
            return -1;
        fl |= FD_CLOEXEC;
        if (fcntl(newfd, F_SETFD, fl) == -1)
            return -1;
    }

    return r;
}
#endif
