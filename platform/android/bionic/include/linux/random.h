#pragma once

#include <stddef.h>
#include <stdint.h>
#include <sys_compat/muplar_errno.h>
#include <string.h>
#include <stdlib.h> // for arc4random_buf

/* Linux-like getrandom on macOS */
static inline ssize_t getrandom(void *buf, size_t buflen, unsigned int flags) {
    (void)flags; // macOS ignores flags

    if (!buf) {
        errno = EFAULT;
        return -1;
    }

    arc4random_buf(buf, buflen);
    return (ssize_t)buflen;
}
