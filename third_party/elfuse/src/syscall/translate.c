/* Linux errno and flag translation (macOS <-> Linux)
 *
 * Copyright 2026 elfuse contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdint.h>

#include "utils.h"

#include "syscall/abi.h"
#include "syscall/internal.h"

/* Linux errno translation. */

/* X-macro table: macOS->Linux errno mappings where values differ.
 * Only entries where macOS and Linux numeric values diverge are listed.
 * Errno values 1-34 are (mostly) identical and handled by the default path,
 * except EDEADLK (macOS 11 vs Linux 35) which must be explicit.
 *
 * Format: _(macOS_errno, Linux_errno)
 */
/* clang-format off */
#define ERRNO_MAP(_)                                                     \
    /* POSIX divergent values */                                         \
    _(EDEADLK,         LINUX_EDEADLK)         /* mac 11 -> linux 35 */   \
    _(EAGAIN,          LINUX_EAGAIN)          /* mac 35 -> linux 11 */   \
    _(EINPROGRESS,     LINUX_EINPROGRESS)     /* mac 36 -> linux 115 */  \
    _(EALREADY,        LINUX_EALREADY)        /* mac 37 -> linux 114 */  \
    _(ENOTSOCK,        LINUX_ENOTSOCK)        /* mac 38 -> linux 88 */   \
    _(EDESTADDRREQ,    LINUX_EDESTADDRREQ)    /* mac 39 -> linux 89 */   \
    _(EMSGSIZE,        LINUX_EMSGSIZE)        /* mac 40 -> linux 90 */   \
    _(EPROTOTYPE,      LINUX_EPROTOTYPE)      /* mac 41 -> linux 91 */   \
    _(ENOPROTOOPT,     LINUX_ENOPROTOOPT)     /* mac 42 -> linux 92 */   \
    _(EPROTONOSUPPORT, LINUX_EPROTONOSUPPORT) /* mac 43 -> linux 93 */   \
    _(ESOCKTNOSUPPORT, LINUX_ESOCKTNOSUPPORT) /* mac 44 -> linux 94 */   \
    _(EAFNOSUPPORT,    LINUX_EAFNOSUPPORT)    /* mac 47 -> linux 97 */   \
    _(EADDRINUSE,      LINUX_EADDRINUSE)      /* mac 48 -> linux 98 */   \
    _(EADDRNOTAVAIL,   LINUX_EADDRNOTAVAIL)   /* mac 49 -> linux 99 */   \
    _(ENETDOWN,        LINUX_ENETDOWN)        /* mac 50 -> linux 100 */  \
    _(ENETUNREACH,     LINUX_ENETUNREACH)     /* mac 51 -> linux 101 */  \
    _(ENETRESET,       LINUX_ENETRESET)       /* mac 52 -> linux 102 */  \
    _(ECONNABORTED,    LINUX_ECONNABORTED)    /* mac 53 -> linux 103 */  \
    _(ECONNRESET,      LINUX_ECONNRESET)      /* mac 54 -> linux 104 */  \
    _(ENOBUFS,         LINUX_ENOBUFS)         /* mac 55 -> linux 105 */  \
    _(EISCONN,         LINUX_EISCONN)         /* mac 56 -> linux 106 */  \
    _(ENOTCONN,        LINUX_ENOTCONN)        /* mac 57 -> linux 107 */  \
    _(ESHUTDOWN,       LINUX_ESHUTDOWN)       /* mac 58 -> linux 108 */  \
    _(ETOOMANYREFS,    LINUX_ETOOMANYREFS)    /* mac 59 -> linux 109 */  \
    _(ETIMEDOUT,       LINUX_ETIMEDOUT)       /* mac 60 -> linux 110 */  \
    _(ECONNREFUSED,    LINUX_ECONNREFUSED)    /* mac 61 -> linux 111 */  \
    _(ELOOP,           LINUX_ELOOP)           /* mac 62 -> linux 40 */   \
    _(ENAMETOOLONG,    LINUX_ENAMETOOLONG)    /* mac 63 -> linux 36 */   \
    _(EHOSTDOWN,       LINUX_EHOSTDOWN)       /* mac 64 -> linux 112 */  \
    _(EHOSTUNREACH,    LINUX_EHOSTUNREACH)    /* mac 65 -> linux 113 */  \
    _(ENOTEMPTY,       LINUX_ENOTEMPTY)       /* mac 66 -> linux 39 */   \
    _(EDQUOT,          LINUX_EDQUOT)          /* mac 69 -> linux 122 */  \
    _(ESTALE,          LINUX_ESTALE)          /* mac 70 -> linux 116 */  \
    _(ENOLCK,          LINUX_ENOLCK)          /* mac 77 -> linux 37 */   \
    _(ENOSYS,          LINUX_ENOSYS)          /* mac 78 -> linux 38 */   \
    _(EOVERFLOW,       LINUX_EOVERFLOW)       /* mac 84 -> linux 75 */   \
    _(EILSEQ,          LINUX_EILSEQ)          /* mac 92 -> linux 84 */   \
    _(ENOMSG,          LINUX_ENOMSG)          /* mac 91 -> linux 42 */   \
    _(EMULTIHOP,       LINUX_EMULTIHOP)       /* mac 95 -> linux 72 */   \
    _(ENOLINK,         LINUX_ENOLINK)         /* mac 97 -> linux 67 */   \
    _(EPROTO,          LINUX_EPROTO)          /* mac 100 -> linux 71 */  \
    _(EOPNOTSUPP,      LINUX_EOPNOTSUPP)      /* mac 102 -> linux 95 */
/* clang-format on */

/* Convert macOS errno to the equivalent Linux errno value.
 * macOS and Linux errno values diverge starting around errno 35.
 * Returns the negative Linux errno for direct use as a syscall return.
 */
int64_t linux_errno(void)
{
    int e = errno;

    switch (e) {
        /* clang-format off */
#define _(mac_err, linux_err) \
    case mac_err:             \
        return -(linux_err);
        ERRNO_MAP(_)
#undef _
        /* clang-format on */

        /* ENOTSUP and EOPNOTSUPP are distinct on modern macOS
         * (__DARWIN_UNIX03). Both map to Linux EOPNOTSUPP (95).
         */
#if ENOTSUP != EOPNOTSUPP
    case ENOTSUP:
        return -LINUX_EOPNOTSUPP;
#endif
#ifdef ENOTRECOVERABLE
    case ENOTRECOVERABLE:
        return -LINUX_ENOTRECOVERABLE;
#endif
#ifdef EOWNERDEAD
    case EOWNERDEAD:
        return -LINUX_EOWNERDEAD;
#endif
    default:
        /* Errno values 1-34 are numerically identical between macOS and Linux
         * (except macOS EDEADLK=11 which maps to Linux EDEADLK=35, handled
         * above).
         */
        if (RANGE_CHECK(e, 1, 34))
            return -(int64_t) e;
        return -LINUX_EINVAL;
    }
}

/* Linux AT_* flags translation. */

/* Translate Linux AT_* flags to macOS equivalents.
 * For unlinkat, fstatat, linkat, fchmodat, fchownat, utimensat.
 * Linux and macOS use different values for AT_SYMLINK_NOFOLLOW etc.
 */
int translate_at_flags(int linux_flags)
{
    int mac_flags = 0;
    if (linux_flags & LINUX_AT_SYMLINK_NOFOLLOW)
        mac_flags |= AT_SYMLINK_NOFOLLOW;
    if (linux_flags & LINUX_AT_SYMLINK_FOLLOW)
        mac_flags |= AT_SYMLINK_FOLLOW;
    if (linux_flags & LINUX_AT_REMOVEDIR)
        mac_flags |= AT_REMOVEDIR;
    /* AT_EMPTY_PATH not supported on macOS */
    return mac_flags;
}

/* Translate Linux faccessat flags to macOS equivalents.
 * Linux AT_EACCESS (0x200) shares the same value as AT_REMOVEDIR; the meaning
 * is context-dependent: 0x200 means AT_REMOVEDIR for unlinkat, but AT_EACCESS
 * for faccessat.
 */
int translate_faccessat_flags(int linux_flags)
{
    int mac_flags = 0;
    if (linux_flags & LINUX_AT_EACCESS)
        mac_flags |= AT_EACCESS;
    if (linux_flags & LINUX_AT_SYMLINK_NOFOLLOW)
        mac_flags |= AT_SYMLINK_NOFOLLOW;
    return mac_flags;
}

/* Linux open flags translation. */

/* X-macro table: Linux open flag -> macOS open flag (1:1 bit mappings).
 * Flags that have no macOS equivalent (O_LARGEFILE, O_DIRECT) are not listed
 * and are silently dropped.
 *
 * Format: _(linux_flag, macos_flag)
 */
/* clang-format off */
#define OPEN_FLAG_MAP(_)              \
    _(LINUX_O_CREAT,     O_CREAT)     \
    _(LINUX_O_EXCL,      O_EXCL)      \
    _(LINUX_O_TRUNC,     O_TRUNC)     \
    _(LINUX_O_APPEND,    O_APPEND)    \
    _(LINUX_O_NONBLOCK,  O_NONBLOCK)  \
    _(LINUX_O_NOFOLLOW,  O_NOFOLLOW)  \
    _(LINUX_O_CLOEXEC,   O_CLOEXEC)   \
    _(LINUX_O_DIRECTORY, O_DIRECTORY) \
    _(LINUX_O_NOCTTY,    O_NOCTTY)
/* clang-format on */

int translate_open_flags(int linux_flags)
{
    int flags = 0;

    if (linux_flags & LINUX_O_PATH) {
        flags |= O_RDONLY;
        if (linux_flags & LINUX_O_CLOEXEC)
            flags |= O_CLOEXEC;
        if (linux_flags & LINUX_O_DIRECTORY)
            flags |= O_DIRECTORY;
        if (linux_flags & LINUX_O_NOFOLLOW) {
#ifdef O_SYMLINK
            flags |= O_SYMLINK;
#else
            flags |= O_NOFOLLOW;
#endif
        }
        return flags;
    }

    /* Access mode (low 2 bits): values match between Linux and macOS */
    int accmode = linux_flags & 3;
    if (accmode == LINUX_O_RDONLY)
        flags |= O_RDONLY;
    else if (accmode == LINUX_O_WRONLY)
        flags |= O_WRONLY;
    else if (accmode == LINUX_O_RDWR)
        flags |= O_RDWR;

    /* clang-format off */
#define _(lf, mf)           \
    if (linux_flags & (lf)) \
        flags |= (mf);
    OPEN_FLAG_MAP(_)
#undef _
    /* clang-format on */

    return flags;
}

/* Translate macOS status flags (from fcntl F_GETFL) to Linux equivalents.
 * Only status flags differ; access mode (low 2 bits) is the same.
 */
int mac_to_linux_status_flags(int mac_flags)
{
    int linux_flags = mac_flags & O_ACCMODE; /* O_RDONLY/WRONLY/RDWR same */
    if (mac_flags & O_NONBLOCK)
        linux_flags |= LINUX_O_NONBLOCK;
    if (mac_flags & O_APPEND)
        linux_flags |= LINUX_O_APPEND;
    if (mac_flags & O_ASYNC)
        linux_flags |= 0x2000; /* LINUX_O_ASYNC */
    return linux_flags;
}

/* Translate Linux status flags (for fcntl F_SETFL) to macOS equivalents.
 * F_SETFL only modifies status flags, not access mode or creation flags.
 */
int linux_to_mac_status_flags(int linux_flags)
{
    int mac_flags = 0;
    if (linux_flags & LINUX_O_NONBLOCK)
        mac_flags |= O_NONBLOCK;
    if (linux_flags & LINUX_O_APPEND)
        mac_flags |= O_APPEND;
    if (linux_flags & 0x2000)
        mac_flags |= O_ASYNC; /* LINUX_O_ASYNC */
    return mac_flags;
}
