/* linux/tiocl.h — macOS-compatible terminal ioctls */
#pragma once

#ifdef __linux__
#include <linux/tiocl.h>
#else
#include <termios.h>
#include <unistd.h>
#include <stdint.h>
#include <sys_compat/muplar_errno.h>

/* Linux constants — define any used in your code */
#define TIOCL_GET 1
#define TIOCL_SET 2
#define TIOCL_STAR 3   /* example placeholder */

/* Terminal state storage */
static struct termios __tiocl_orig;

/* Save terminal state */
static inline int tiocl_save(int fd) {
    if (tcgetattr(fd, &__tiocl_orig) < 0) return -errno;
    return 0;
}

/* Restore terminal state */
static inline int tiocl_restore(int fd) {
    if (tcsetattr(fd, TCSANOW, &__tiocl_orig) < 0) return -errno;
    return 0;
}

/* Replace Linux ioctl(fd, TIOCL_*) */
static inline int tiocl_ioctl(int fd, int request, void *arg) {
    struct termios t;
    if (tcgetattr(fd, &t) < 0) return -errno;

    switch (request) {
        case TIOCL_GET:
            /* emulate returning current flags if needed */
            if (arg) *(int *)arg = 0;  /* stub value */
            return 0;

        case TIOCL_SET:
            /* emulate setting flags if needed */
            /* for example, enable/disable echo, raw mode, etc. */
            if (arg) {
                int flags = *(int *)arg;
                if (flags & 1) cfmakeraw(&t); /* raw mode example */
                /* add more flags mapping if needed */
                if (tcsetattr(fd, TCSANOW, &t) < 0) return -errno;
            }
            return 0;

        default:
            return -ENOSYS;  /* not implemented */
    }
}
#endif
