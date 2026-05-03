/* linux/vt.h — macOS compatibility layer */
#pragma once

#include <stdint.h>
#include <sys_compat/muplar_errno.h>
#include <unistd.h>

/* Linux VT constants (dummy values) */
#define VT_GETSTATE 1
#define VT_ACTIVATE 2
#define VT_WAITACTIVE 3
#define VT_DISALLOCATE 4

/* Linux struct vt_stat stub */
struct vt_stat {
    unsigned short v_active;
    unsigned short v_signal;
    unsigned short v_state;
};

/* Stub functions for VT ioctls */
static inline int vt_ioctl(int fd, int request, void *arg) {
    (void)fd;
    (void)arg;

    switch(request) {
        case VT_GETSTATE:
            if (arg) {
                struct vt_stat *st = (struct vt_stat *)arg;
                st->v_active = 1;
                st->v_signal = 0;
                st->v_state = 0;
            }
            return 0;
        case VT_ACTIVATE:
        case VT_WAITACTIVE:
        case VT_DISALLOCATE:
            /* nothing to do on macOS */
            return 0;
        default:
            return -ENOSYS;
    }
}

