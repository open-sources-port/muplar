#pragma once

#include <stdio.h>
#include <string.h>
#include <sys_compat/muplar_errno.h>

#if defined(__APPLE__)
#include <crt_externs.h>

/* GNU-specific global replacement */
const char *program_invocation_short_name_fallback(void);

#define program_invocation_short_name program_invocation_short_name_fallback()

/* portable strerror_r wrapper */
static inline char *strerror_r_wrap(int err, char *buf, size_t buflen) {
    /* macOS / POSIX version returns int */
    strerror_r(err, buf, buflen);
    return buf;
}

#else

#define strerror_r_wrap strerror_r
#endif
