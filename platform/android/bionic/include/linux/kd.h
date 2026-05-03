/* linux/kd.h — macOS compatibility layer */
#pragma once

#ifdef __linux__
#include <linux/kd.h>
#else
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>
#include <sys_compat/muplar_errno.h>
#include <stdint.h>

/* Linux constants */
#define KDSETMODE 0
#define K_RAW 0
#define K_XLATE 0
#define KDMKTONE 0

/* Terminal state storage */
static struct termios __term_orig;

/* Helper: emulate Linux KDSETMODE behavior */
static inline int kd_ioctl(int fd, int request, intptr_t arg) {
    if (request == KDSETMODE) {
        if (arg == K_RAW) {
            struct termios t;
            if (tcgetattr(fd, &t) < 0) return -errno;
            cfmakeraw(&t);
            if (tcsetattr(fd, TCSANOW, &t) < 0) return -errno;
        } else if (arg == K_XLATE) {
            if (tcsetattr(fd, TCSANOW, &__term_orig) < 0) return -errno;
        }
        return 0;
    } else if (request == KDMKTONE) {
        /* just beep */
        write(fd, "\a", 1);
        return 0;
    }
    return -ENOSYS;
}

/* Save terminal state */
static inline int terminal_save(int fd) {
    if (tcgetattr(fd, &__term_orig) < 0) return -errno;
    return 0;
}
#endif
