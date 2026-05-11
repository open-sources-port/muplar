/* Filesystem syscall handlers
 *
 * Copyright 2026 elfuse contributors
 * Copyright 2025 Moritz Angermann, zw3rk pte. ltd.
 * SPDX-License-Identifier: Apache-2.0
 *
 * Stat, open, close, directory, permissions, and other filesystem
 * operations. All functions are called from syscall_dispatch() in
 * syscall/syscall.c.
 */

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/mount.h>
#include <pthread.h>

#include "debug/log.h"
#include "utils.h"

#include "runtime/procemu.h"

#include "syscall/abi.h"
#include "syscall/fs.h"
#include "syscall/internal.h"
#include "syscall/net.h" /* absock_unregister_fd */
#include "syscall/path.h"
#include "syscall/proc.h"

/* Linux dirent64 layout. */
typedef struct {
    uint64_t d_ino;
    int64_t d_off;
    uint16_t d_reclen;
    uint8_t d_type;
    /* char d_name[] follows */
} PACKED linux_dirent64_t;

static int opened_fd_type(int host_fd, int linux_flags)
{
    struct stat st;
    if (fstat(host_fd, &st) < 0)
        return -1;
    bool is_dir = S_ISDIR(st.st_mode);
    if ((linux_flags & LINUX_O_DIRECTORY) && !is_dir) {
        errno = ENOTDIR;
        return -1;
    }
    if (linux_flags & LINUX_O_PATH)
        return FD_PATH;
    if (is_dir)
        return FD_DIR;

    return FD_REGULAR;
}

static const char *proc_virtual_dir_path(const char *path,
                                         char *buf,
                                         size_t bufsz);

static const char *proc_stateful_file_path(const char *path)
{
    if (!path || strncmp(path, "/proc/", 6) != 0)
        return NULL;

    if (!strcmp(path, "/proc/self/oom_score_adj") ||
        !strcmp(path, "/proc/self/oom_adj") ||
        !strcmp(path, "/proc/self/oom_score")) {
        return path;
    }

    char *endp;
    long pid = strtol(path + 6, &endp, 10);
    if (endp == path + 6 || pid != (long) proc_get_pid())
        return NULL;

    if (!strcmp(endp, "/oom_score_adj"))
        return "/proc/self/oom_score_adj";
    if (!strcmp(endp, "/oom_adj"))
        return "/proc/self/oom_adj";
    if (!strcmp(endp, "/oom_score"))
        return "/proc/self/oom_score";

    return NULL;
}

static void fd_note_proc_path(int guest_fd, const char *path)
{
    if (!path || strncmp(path, "/proc", 5) != 0)
        return;

    char virt_buf[64];
    const char *virt = proc_virtual_dir_path(path, virt_buf, sizeof(virt_buf));
    if (!virt)
        virt = proc_stateful_file_path(path);

    if (virt)
        str_copy_trunc(fd_table[guest_fd].proc_path, virt,
                       sizeof(fd_table[guest_fd].proc_path));
}

static const char *proc_virtual_dir_path(const char *path,
                                         char *buf,
                                         size_t bufsz)
{
    if (!path || strncmp(path, "/proc", 5) != 0)
        return NULL;

    const char *virt = NULL;
    if (!strcmp(path, "/proc") || !strcmp(path, "/proc/")) {
        virt = "/proc";
    } else if (!strcmp(path, "/proc/self") || !strcmp(path, "/proc/self/")) {
        virt = "/proc/self";
    } else if (!strcmp(path, "/proc/net") || !strcmp(path, "/proc/net/")) {
        virt = "/proc/net";
    } else if (!strcmp(path, "/proc/self/fd") ||
               !strcmp(path, "/proc/self/fd/")) {
        virt = "/proc/self/fd";
    } else if (!strcmp(path, "/proc/self/fdinfo") ||
               !strcmp(path, "/proc/self/fdinfo/")) {
        virt = "/proc/self/fdinfo";
    } else if (!strcmp(path, "/proc/self/task") ||
               !strcmp(path, "/proc/self/task/")) {
        virt = "/proc/self/task";
    } else if (!strncmp(path, "/proc/self/task/", 16)) {
        char *endp;
        long tid = strtol(path + 16, &endp, 10);
        if (endp != path + 16 && tid > 0 &&
            (*endp == '\0' || !strcmp(endp, "/"))) {
            snprintf(buf, bufsz, "/proc/self/task/%ld", tid);
            virt = buf;
        }
    } else if (!strncmp(path, "/proc/", 6)) {
        char *endp;
        long pid = strtol(path + 6, &endp, 10);
        if (endp != path + 6 && pid == (long) proc_get_pid() &&
            (*endp == '\0' || !strcmp(endp, "/"))) {
            virt = "/proc/self";
        } else if (endp != path + 6 && pid == (long) proc_get_pid() &&
                   (!strcmp(endp, "/fdinfo") || !strcmp(endp, "/fdinfo/"))) {
            virt = "/proc/self/fdinfo";
        } else if (endp != path + 6 && pid == (long) proc_get_pid() &&
                   !strcmp(endp, "/fd")) {
            virt = "/proc/self/fd";
        } else if (endp != path + 6 && pid == (long) proc_get_pid() &&
                   !strcmp(endp, "/task")) {
            virt = "/proc/self/task";
        } else if (endp != path + 6 && pid == (long) proc_get_pid() &&
                   !strncmp(endp, "/task/", 6)) {
            char *tid_endp;
            long tid = strtol(endp + 6, &tid_endp, 10);
            if (tid_endp != endp + 6 &&
                (*tid_endp == '\0' || !strcmp(tid_endp, "/"))) {
                snprintf(buf, bufsz, "/proc/self/task/%ld", tid);
                virt = buf;
            }
        }
    }

    return virt;
}

static int dup_fd_type(int guest_fd)
{
    return fd_table[guest_fd].type == FD_STDIO ? FD_REGULAR
                                               : fd_table[guest_fd].type;
}

static int fd_alloc_opened_host(int host_fd,
                                int type,
                                int linux_flags,
                                int min_guest_fd)
{
    DIR *dir = NULL;

    if (type == FD_DIR) {
        int dup_fd = dup(host_fd);
        if (dup_fd < 0)
            return -1;

        dir = fdopendir(dup_fd);
        if (!dir) {
            close_keep_errno(dup_fd);
            return -1;
        }
    }

    int guest_fd = min_guest_fd >= 0
                       ? fd_alloc_from_relaxed(min_guest_fd, type, host_fd)
                       : fd_alloc_from_relaxed(0, type, host_fd);
    if (guest_fd < 0) {
        int saved_errno = errno;
        if (dir)
            closedir(dir);
        errno = saved_errno;
        return -1;
    }

    fd_table[guest_fd].linux_flags = linux_flags;
    if (dir)
        fd_table[guest_fd].dir = dir;
    return guest_fd;
}

/* open/close. */

int64_t sys_openat_path(guest_t *g,
                        int dirfd,
                        const char *pathp,
                        int linux_flags,
                        int mode)
{
    char proc_path[LINUX_PATH_MAX];
    const char *guest_path = pathp;
    const char *intercept_path = pathp;
    int proc_resolved =
        resolve_proc_at_path(dirfd, pathp, proc_path, sizeof(proc_path));
    if (proc_resolved < 0)
        return -LINUX_ENAMETOOLONG;
    if (proc_resolved > 0) {
        guest_path = proc_path;
        intercept_path = proc_path;
    }

    int flags = translate_open_flags(linux_flags);
    if (proc_resolved == 0 && dirfd == LINUX_AT_FDCWD && pathp[0] != '/' &&
        !proc_get_sysroot()) {
        int host_fd = openat(AT_FDCWD, pathp, flags, mode);
        if (host_fd < 0)
            return linux_errno();

        int type = opened_fd_type(host_fd, linux_flags);
        if (type < 0) {
            close_keep_errno(host_fd);
            return linux_errno();
        }
        int guest_fd = fd_alloc_opened_host(host_fd, type, linux_flags, -1);
        if (guest_fd < 0) {
            close_keep_errno(host_fd);
            return linux_errno();
        }
        return guest_fd;
    }

    /* Intercept /proc and /dev paths before touching the host filesystem */
    if (path_might_use_open_intercept(intercept_path)) {
        int intercepted =
            proc_intercept_open(g, intercept_path, linux_flags, mode);
        if (intercepted >= 0) {
            /* Got a host fd from the intercept. Device nodes (/dev/...) use
             * fd_alloc() for POSIX lowest-fd semantics because busybox sh
             * relies on close(0)+open("/dev/null") returning fd 0. Synthetic
             * /proc files use fd_alloc_from(128) to avoid races with
             * concurrent GC finalizers that may close stale low-numbered fds.
             */
            int type = opened_fd_type(intercepted, linux_flags);
            if (type < 0) {
                close_keep_errno(intercepted);
                return linux_errno();
            }
            int min_guest_fd =
                (!strncmp(intercept_path, "/dev/", 5)) ? -1 : 128;
            int guest_fd = fd_alloc_opened_host(intercepted, type, linux_flags,
                                                min_guest_fd);
            if (guest_fd < 0) {
                close_keep_errno(intercepted);
                return linux_errno();
            }
            fd_note_proc_path(guest_fd, intercept_path);
            return guest_fd;
        }
        if (intercepted == -1) {
            /* Intercept matched but failed */
            return linux_errno();
        }
        /* intercepted == PROC_NOT_INTERCEPTED: fall through to real openat */
    }

    char sysroot_buf[LINUX_PATH_MAX];
    const char *open_path =
        (linux_flags & LINUX_O_NOFOLLOW)
            ? path_resolve_sysroot_nofollow_path(guest_path, sysroot_buf,
                                                 sizeof(sysroot_buf))
            : path_resolve_sysroot_path(guest_path, sysroot_buf,
                                        sizeof(sysroot_buf));
    if (!open_path)
        return -LINUX_ENAMETOOLONG;

    if (dirfd == LINUX_AT_FDCWD) {
        int host_fd = open(open_path, flags, mode);
        if (host_fd < 0)
            return linux_errno();

        int type = opened_fd_type(host_fd, linux_flags);
        if (type < 0) {
            close_keep_errno(host_fd);
            return linux_errno();
        }
        int guest_fd = fd_alloc_opened_host(host_fd, type, linux_flags, -1);
        if (guest_fd < 0) {
            close_keep_errno(host_fd);
            return linux_errno();
        }
        return guest_fd;
    }

    host_fd_ref_t dir_ref;
    if (host_dirfd_ref_open(dirfd, &dir_ref) < 0)
        return -LINUX_EBADF;

    int host_fd = openat(dir_ref.fd, open_path, flags, mode);
    host_fd_ref_close(&dir_ref);
    if (host_fd < 0)
        return linux_errno();

    int type = opened_fd_type(host_fd, linux_flags);
    if (type < 0) {
        close_keep_errno(host_fd);
        return linux_errno();
    }
    int guest_fd = fd_alloc_opened_host(host_fd, type, linux_flags, -1);
    if (guest_fd < 0) {
        close_keep_errno(host_fd);
        return linux_errno();
    }
    return guest_fd;
}

int64_t sys_openat(guest_t *g,
                   int dirfd,
                   uint64_t path_gva,
                   int linux_flags,
                   int mode)
{
    char short_path[64];
    char path[LINUX_PATH_MAX];
    const char *pathp;
    if (guest_read_path(g, path_gva, short_path, sizeof(short_path), path,
                        sizeof(path), &pathp) < 0)
        return -LINUX_EFAULT;
    return sys_openat_path(g, dirfd, pathp, linux_flags, mode);
}

int64_t sys_close(int fd)
{
    if (!RANGE_CHECK(fd, 0, FD_TABLE_SIZE))
        return -LINUX_EBADF;

    /* Clean up abstract socket filesystem entry if this fd owns one */
    absock_unregister_fd(fd);

    int host_fd = -1;
    if (fd_close_regular_relaxed(fd, &host_fd)) {
        if (close(host_fd) < 0)
            return linux_errno();
        return 0;
    }

    /* Atomically snapshot and mark closed under fd_lock.  This prevents
     * a TOCTOU race where two concurrent sys_close() calls both read
     * the same open entry and double-close the host fd.
     */
    fd_entry_t snap;
    if (!fd_snapshot_and_close_relaxed(fd, &snap))
        return -LINUX_EBADF;

    fd_cleanup_entry(fd, &snap);
    return 0;
}

/* dup/fcntl. */

static int clone_dir_stream_if_needed(int src_fd, int dst_fd, int dst_host_fd)
{
    if (fd_table[src_fd].type != FD_DIR)
        return 0;

    int dir_fd = dup(dst_host_fd);
    if (dir_fd < 0)
        return -1;

    DIR *dir = fdopendir(dir_fd);
    if (!dir) {
        close(dir_fd);
        return -1;
    }

    fd_table[dst_fd].dir = dir;
    return 0;
}

static void discard_allocated_fd(int guest_fd)
{
    fd_entry_t snap;
    if (fd_snapshot_and_close(guest_fd, &snap))
        fd_cleanup_entry(guest_fd, &snap);
}

static void copy_fd_alias_metadata(int src_fd, int dst_fd, int linux_flags)
{
    int preserved_flags = fd_table[src_fd].linux_flags &
                          (LINUX_O_PATH | LINUX_O_DIRECTORY | LINUX_O_NOFOLLOW |
                           LINUX_O_DIRECT | LINUX_O_LARGEFILE);
    fd_table[dst_fd].linux_flags = preserved_flags | linux_flags;
    fd_table[dst_fd].seals = fd_table[src_fd].seals;
    memcpy(fd_table[dst_fd].proc_path, fd_table[src_fd].proc_path,
           sizeof(fd_table[dst_fd].proc_path));
}

/* Duplicate a guest fd into either the next free slot >= min_guest_fd or a
 * fixed slot. The helper keeps fd metadata copying and directory-stream
 * cloning in one place so dup(), dup3(), and fcntl(F_DUPFD*) stay consistent.
 */
static int duplicate_guest_fd(int src_fd,
                              int min_guest_fd,
                              int fixed_guest_fd,
                              bool fixed_slot,
                              int linux_flags)
{
    host_fd_ref_t host_ref;
    if (host_fd_ref_open(src_fd, &host_ref) < 0) {
        errno = EBADF;
        return -1;
    }

    int new_type = dup_fd_type(src_fd);
    int new_host_fd = dup(host_ref.fd);
    host_fd_ref_close(&host_ref);
    if (new_host_fd < 0)
        return -1;

    int guest_fd =
        fixed_slot ? fd_alloc_at_relaxed(fixed_guest_fd, new_type, new_host_fd)
                   : fd_alloc_from_relaxed(min_guest_fd, new_type, new_host_fd);
    if (guest_fd < 0) {
        if (fixed_slot)
            errno = EBADF;
        close_keep_errno(new_host_fd);
        return -1;
    }

    copy_fd_alias_metadata(src_fd, guest_fd, linux_flags);
    if (clone_dir_stream_if_needed(src_fd, guest_fd, new_host_fd) < 0) {
        int saved_errno = errno;
        discard_allocated_fd(guest_fd);
        errno = saved_errno;
        return -1;
    }

    return guest_fd;
}

static void fd_set_seals_for_aliases(int fd, int host_fd, int seals)
{
    struct stat target;
    if (fstat(host_fd, &target) < 0) {
        fd_table[fd].seals = seals;
        return;
    }

    pthread_mutex_lock(&fd_lock);
    for (int i = 0; i < FD_TABLE_SIZE; i++) {
        if (fd_table[i].type == FD_CLOSED)
            continue;

        struct stat candidate;
        if (fstat(fd_table[i].host_fd, &candidate) == 0 &&
            candidate.st_dev == target.st_dev &&
            candidate.st_ino == target.st_ino) {
            fd_table[i].seals = seals;
        }
    }
    pthread_mutex_unlock(&fd_lock);
}

int64_t sys_dup(int oldfd)
{
    int guest_fd = duplicate_guest_fd(oldfd, 0, -1, false, 0);
    if (guest_fd < 0)
        return linux_errno();
    return guest_fd;
}

int64_t sys_dup3(int oldfd, int newfd, int linux_flags)
{
    if (linux_flags & ~LINUX_O_CLOEXEC)
        return -LINUX_EINVAL;
    if (!RANGE_CHECK(oldfd, 0, FD_TABLE_SIZE))
        return -LINUX_EBADF;
    if (!RANGE_CHECK(newfd, 0, FD_TABLE_SIZE))
        return -LINUX_EBADF;
    /* Linux dup3(2): EINVAL if oldfd == newfd (unlike dup2 which is a no-op) */
    if (oldfd == newfd)
        return -LINUX_EINVAL;
    if (duplicate_guest_fd(oldfd, 0, newfd, true,
                           linux_flags & LINUX_O_CLOEXEC) < 0)
        return linux_errno();
    return newfd;
}

int64_t sys_fcntl(guest_t *g, int fd, int cmd, uint64_t arg)
{
    if (!RANGE_CHECK(fd, 0, FD_TABLE_SIZE))
        return -LINUX_EBADF;

    /* Linux F_DUPFD=0, F_GETFD=1, F_SETFD=2, F_GETFL=3, F_SETFL=4,
     * F_DUPFD_CLOEXEC=1030
     */
    switch (cmd) {
    case 0: /* F_DUPFD */
    case 1030: /* F_DUPFD_CLOEXEC */ {
        if ((int) arg < 0) {
            return -LINUX_EINVAL;
        }
        int dup_flags = fd_table[fd].linux_flags & ~LINUX_O_CLOEXEC;
        if (cmd == 1030)
            dup_flags |= LINUX_O_CLOEXEC;
        int gfd = duplicate_guest_fd(fd, (int) arg, -1, false, dup_flags);
        if (gfd < 0) {
            if (errno == EBADF)
                return -LINUX_EBADF;
            return -LINUX_EMFILE;
        }
        return gfd;
    }
    case 1: /* F_GETFD */
        return (fd_table[fd].linux_flags & LINUX_O_CLOEXEC) ? LINUX_FD_CLOEXEC
                                                            : 0;
    case 2: /* F_SETFD */
        if ((int) arg & LINUX_FD_CLOEXEC)
            fd_table[fd].linux_flags |= LINUX_O_CLOEXEC;
        else
            fd_table[fd].linux_flags &= ~LINUX_O_CLOEXEC;
        return 0;
    case 3: { /* F_GETFL */
        fd_entry_t snap;
        if (!fd_snapshot(fd, &snap))
            return -LINUX_EBADF;
        host_fd_ref_t host_ref;
        if (host_fd_ref_open(fd, &host_ref) < 0)
            return -LINUX_EBADF;
        int mac_fl = fcntl(host_ref.fd, F_GETFL);
        host_fd_ref_close(&host_ref);
        if (mac_fl < 0)
            return linux_errno();
        int linux_fl = mac_to_linux_status_flags(mac_fl);
        if (snap.type == FD_REGULAR || snap.type == FD_DIR ||
            snap.type == FD_PATH)
            linux_fl = (linux_fl & ~O_ACCMODE) | (snap.linux_flags & 3);
        linux_fl |= snap.linux_flags &
                    (LINUX_O_PATH | LINUX_O_DIRECTORY | LINUX_O_NOFOLLOW |
                     LINUX_O_DIRECT | LINUX_O_LARGEFILE);
        return linux_fl;
    }
    case 4: /* F_SETFL */
    {
        host_fd_ref_t host_ref;
        if (host_fd_ref_open(fd, &host_ref) < 0)
            return -LINUX_EBADF;
        int rc =
            fcntl(host_ref.fd, F_SETFL, linux_to_mac_status_flags((int) arg));
        host_fd_ref_close(&host_ref);
        return rc < 0 ? linux_errno() : 0;
    }
    case 5:   /* F_GETLK */
    case 6:   /* F_SETLK */
    case 7: { /* F_SETLKW */
        host_fd_ref_t host_ref;
        if (host_fd_ref_open(fd, &host_ref) < 0)
            return -LINUX_EBADF;
        /* Translate Linux struct flock (aarch64) to macOS struct flock.
         * Linux aarch64 layout: {short l_type, short l_whence,
         *   long l_start, long l_len, int l_pid, pad[4]}
         * macOS layout: {off_t l_start, off_t l_len, pid_t l_pid,
         *   short l_type, short l_whence}
         * Use guest_read/guest_write (not guest_ptr) to safely handle
         * structs that span 2MiB page table block boundaries.
         */
        uint8_t lflock[32]; /* Linux struct flock is 32 bytes on aarch64 */
        if (guest_read_small(g, arg, lflock, sizeof(lflock)) < 0)
            return -LINUX_EFAULT;

        /* Read Linux flock fields */
        int16_t l_type, l_whence;
        int64_t l_start, l_len;
        memcpy(&l_type, lflock + 0, 2);
        memcpy(&l_whence, lflock + 2, 2);
        memcpy(&l_start, lflock + 8, 8); /* offset 8 due to padding */
        memcpy(&l_len, lflock + 16, 8);

        struct flock mac_fl = {
            .l_start = l_start,
            .l_len = l_len,
            .l_pid = 0,
            .l_type = l_type, /* F_RDLCK=0, F_WRLCK=1, F_UNLCK=2 same on both */
            .l_whence = l_whence, /* SEEK_SET=0, SEEK_CUR=1, SEEK_END=2 same */
        };

        int mac_cmd = (cmd == 5) ? F_GETLK : (cmd == 6) ? F_SETLK : F_SETLKW;
        if (fcntl(host_ref.fd, mac_cmd, &mac_fl) < 0) {
            host_fd_ref_close(&host_ref);
            return linux_errno();
        }

        /* For F_GETLK, write back the result */
        if (cmd == 5) {
            int16_t rt = mac_fl.l_type, rw = mac_fl.l_whence;
            int64_t rs = mac_fl.l_start, rl = mac_fl.l_len;
            int32_t rp = mac_fl.l_pid;
            memset(lflock, 0, sizeof(lflock));
            memcpy(lflock + 0, &rt, 2);
            memcpy(lflock + 2, &rw, 2);
            memcpy(lflock + 8, &rs, 8);
            memcpy(lflock + 16, &rl, 8);
            memcpy(lflock + 24, &rp, 4);
            if (guest_write_small(g, arg, lflock, sizeof(lflock)) < 0) {
                host_fd_ref_close(&host_ref);
                return -LINUX_EFAULT;
            }
        }
        host_fd_ref_close(&host_ref);
        return 0;
    }
    case 1024: /* F_GETPIPE_SZ */
        /* macOS does not support pipe size queries; return default 64KiB */
        return 65536;
    case 1031: /* F_SETPIPE_SZ */
        /* macOS does not support pipe size setting; pretend success */
        return (int64_t) arg;
    case LINUX_F_GET_SEALS:
        return fd_table[fd].seals;
    case LINUX_F_ADD_SEALS: {
        host_fd_ref_t host_ref;
        if (host_fd_ref_open(fd, &host_ref) < 0)
            return -LINUX_EBADF;
        int cur = fd_table[fd].seals;
        /* Cannot add seals if F_SEAL_SEAL is already set */
        if (cur & LINUX_F_SEAL_SEAL) {
            host_fd_ref_close(&host_ref);
            return -LINUX_EPERM;
        }
        int new_seals = (int) arg;
        /* Only allow valid seal bits */
        if (new_seals &
            ~(LINUX_F_SEAL_SEAL | LINUX_F_SEAL_SHRINK | LINUX_F_SEAL_GROW |
              LINUX_F_SEAL_WRITE | LINUX_F_SEAL_FUTURE_WRITE)) {
            host_fd_ref_close(&host_ref);
            return -LINUX_EINVAL;
        }
        fd_set_seals_for_aliases(fd, host_ref.fd, cur | new_seals);
        host_fd_ref_close(&host_ref);
        return 0;
    }
    default:
        return -LINUX_EINVAL;
    }
}

#define LINUX_CLOSE_RANGE_CLOEXEC 4

int64_t sys_close_range(unsigned int first,
                        unsigned int last,
                        unsigned int flags)
{
    /* Linux returns EINVAL when first > last (even if both are valid) */
    if (first > last)
        return -LINUX_EINVAL;
    /* Reject unknown flags */
    if (flags & ~(unsigned) LINUX_CLOSE_RANGE_CLOEXEC)
        return -LINUX_EINVAL;
    /* Clamp to FD table size (Linux clamps ~0U to NR_OPEN_MAX) */
    if (last >= (unsigned) FD_TABLE_SIZE)
        last = FD_TABLE_SIZE - 1;

    /* CLOSE_RANGE_CLOEXEC: mark FDs as CLOEXEC without closing them.
     * Hold fd_lock to prevent races with concurrent fd_alloc/close.
     */
    if (flags & LINUX_CLOSE_RANGE_CLOEXEC) {
        pthread_mutex_lock(&fd_lock);
        for (unsigned int i = first; i <= last && i < (unsigned) FD_TABLE_SIZE;
             i++) {
            if (fd_table[i].type != FD_CLOSED)
                fd_table[i].linux_flags |= LINUX_O_CLOEXEC;
        }
        pthread_mutex_unlock(&fd_lock);
        return 0;
    }

    for (unsigned int i = first; i <= last && i < (unsigned) FD_TABLE_SIZE;
         i++) {
        fd_entry_t snap;
        if (!fd_snapshot_and_close((int) i, &snap))
            continue;

        fd_cleanup_entry((int) i, &snap);
    }
    return 0;
}

/* directory operations. */

/* getdents64: read directory entries from a guest directory fd.
 * Uses the persistent DIR* stored in fd_table (created by openat).
 */
int64_t sys_getdents64(guest_t *g, int fd, uint64_t buf_gva, uint64_t count)
{
    if (!RANGE_CHECK(fd, 0, FD_TABLE_SIZE))
        return -LINUX_EBADF;
    if (fd_table[fd].type == FD_CLOSED)
        return -LINUX_EBADF;
    /* Linux: getdents on an O_PATH fd returns EBADF, even when the underlying
     * inode is a directory. The early gate keeps the next NOTDIR fallback
     * specific to non-directory regular fds.
     */
    if (fd_table[fd].type == FD_PATH)
        return -LINUX_EBADF;

    DIR *dir = (DIR *) fd_table[fd].dir;
    if (!dir)
        return -LINUX_ENOTDIR;

    if (!guest_ptr(g, buf_gva))
        return -LINUX_EFAULT;

    size_t guest_pos = 0;
    struct dirent *de;

    /* Temp buffer for dirent serialization. Max dirent64 is 280 bytes
     * (19-byte header + NAME_MAX=255 + null + padding to 8). Using a
     * stack buffer avoids guest_ptr boundary issues: guest_write() handles
     * 2MiB block crossings that raw memcpy into guest_ptr() cannot.
     */
    uint8_t entry_buf[280];

    while (1) {
        /* Save position BEFORE readdir so getdents emulation can rewind if the
         * entry does not fit.  macOS telldir returns an opaque cookie --
         * arithmetic on it (e.g. telldir()-1) is undefined.
         */
        long saved_pos = telldir(dir);
        de = readdir(dir);
        if (!de)
            break;

        size_t name_len = strlen(de->d_name);
        /* Linux dirent64: 19-byte header + name + null, padded to 8 */
        size_t reclen = (19 + name_len + 1 + 7) & ~7ULL;

        if (guest_pos + reclen > count) {
            /* Entry does not fit; rewind so next call gets it */
            seekdir(dir, saved_pos);
            break;
        }

        linux_dirent64_t lde;
        lde.d_ino = de->d_ino;
        lde.d_off = telldir(dir);
        lde.d_reclen = (uint16_t) reclen;
        lde.d_type = de->d_type;

        /* Serialize entry into temp buffer, then copy to guest via
         * guest_write() which handles 2MiB block boundary crossings.
         */
        memcpy(entry_buf, &lde, sizeof(lde));
        memcpy(entry_buf + 19, de->d_name, name_len + 1);
        size_t pad_start = 19 + name_len + 1;
        if (pad_start < reclen)
            memset(entry_buf + pad_start, 0, reclen - pad_start);

        if (guest_write(g, buf_gva + guest_pos, entry_buf, reclen) < 0)
            return guest_pos > 0 ? (int64_t) guest_pos : -LINUX_EFAULT;

        guest_pos += reclen;
    }

    return (int64_t) guest_pos;
}

int64_t sys_chdir(guest_t *g, uint64_t path_gva)
{
    char path[LINUX_PATH_MAX], proc_path[LINUX_PATH_MAX];
    if (guest_read_str(g, path_gva, path, sizeof(path)) < 0)
        return -LINUX_EFAULT;

    char proc_virt[64];
    const char *chdir_path = path;
    int proc_resolved = resolve_proc_at_path(LINUX_AT_FDCWD, path, proc_path,
                                             sizeof(proc_path));
    if (proc_resolved < 0)
        return -LINUX_ENAMETOOLONG;
    if (proc_resolved > 0)
        chdir_path = proc_path;

    const char *virt =
        proc_virtual_dir_path(chdir_path, proc_virt, sizeof(proc_virt));
    if (virt) {
        int host_fd = proc_intercept_open(g, chdir_path, LINUX_O_DIRECTORY, 0);
        if (host_fd < 0)
            return linux_errno();
        int rc = fchdir(host_fd);
        int saved_errno = errno;
        close_keep_errno(host_fd);
        if (rc < 0) {
            errno = saved_errno;
            return linux_errno();
        }
        proc_cwd_set_virtual(virt);
        return 0;
    }

    if (chdir(chdir_path) < 0)
        return linux_errno();

    if (proc_cwd_refresh() < 0)
        proc_cwd_invalidate();
    return 0;
}

int64_t sys_fchdir(int fd)
{
    host_fd_ref_t host_ref;
    if (host_fd_ref_open(fd, &host_ref) < 0)
        return -LINUX_EBADF;

    char proc_virt[64];
    const char *proc_virtual = proc_virtual_dir_path(
        fd_table[fd].proc_path, proc_virt, sizeof(proc_virt));
    if (fchdir(host_ref.fd) < 0) {
        host_fd_ref_close(&host_ref);
        return linux_errno();
    }
    host_fd_ref_close(&host_ref);
    if (proc_virtual) {
        proc_cwd_set_virtual(proc_virtual);
    } else if (proc_cwd_refresh() < 0) {
        proc_cwd_invalidate();
    }
    return 0;
}

int64_t sys_chroot(guest_t *g, uint64_t path_gva)
{
    char path[LINUX_PATH_MAX];
    if (guest_read_str(g, path_gva, path, sizeof(path)) < 0)
        return -LINUX_EFAULT;
    /* Emulate chroot by updating the sysroot prefix.  All path resolution
     * already redirects through sysroot.  chroot("/") is a no-op because the
     * guest already sees "/" as root, and resetting sysroot to "/" would
     * break dynamic linker resolution.  Real chroot() requires root and
     * does not make sense in elfuse's single-process VM.  This enables
     * coreutils stdbuf (which does fork -> chroot("/") -> exec) and the
     * chroot coreutil itself.
     */
    if (strcmp(path, "/") != 0) {
        struct stat st;
        if (stat(path, &st) < 0)
            return linux_errno();
        if (!S_ISDIR(st.st_mode))
            return -LINUX_ENOTDIR;
        proc_set_sysroot(path);
    }
    return 0;
}

/* pipe/seek. */

int64_t sys_pipe2(guest_t *g, uint64_t fds_gva, int linux_flags)
{
    if (linux_flags & ~(LINUX_O_CLOEXEC | LINUX_O_NONBLOCK | LINUX_O_DIRECT))
        return -LINUX_EINVAL;

    int host_fds[2];
    if (pipe(host_fds) < 0)
        return linux_errno();

    int guest_fds[2];
    guest_fds[0] = fd_alloc(FD_PIPE, host_fds[0], NULL);
    if (guest_fds[0] < 0) {
        int saved_errno = errno;
        close(host_fds[0]);
        close(host_fds[1]);
        errno = saved_errno;
        return linux_errno();
    }

    guest_fds[1] = fd_alloc(FD_PIPE, host_fds[1], NULL);
    if (guest_fds[1] < 0) {
        int saved_errno = errno;
        fd_mark_closed(guest_fds[0]);
        close(host_fds[0]);
        close(host_fds[1]);
        errno = saved_errno;
        return linux_errno();
    }

    /* Apply O_NONBLOCK to host FDs if requested */
    if (linux_flags & LINUX_O_NONBLOCK) {
        fcntl(host_fds[0], F_SETFL, O_NONBLOCK);
        fcntl(host_fds[1], F_SETFL, O_NONBLOCK);
    }

    /* Propagate O_CLOEXEC if set in flags */
    fd_table[guest_fds[0]].linux_flags = linux_flags & LINUX_O_CLOEXEC;
    fd_table[guest_fds[1]].linux_flags = linux_flags & LINUX_O_CLOEXEC;

    int32_t fds[2] = {guest_fds[0], guest_fds[1]};
    if (guest_write_small(g, fds_gva, fds, sizeof(fds)) < 0) {
        fd_mark_closed(guest_fds[0]);
        fd_mark_closed(guest_fds[1]);
        close(host_fds[0]);
        close(host_fds[1]);
        return -LINUX_EFAULT;
    }

    return 0;
}

int64_t sys_lseek(int fd, int64_t offset, int whence)
{
    host_fd_ref_t host_ref;
    int64_t err = host_fd_ref_open_io(fd, &host_ref);
    if (err < 0)
        return err;

    off_t ret = lseek(host_ref.fd, offset, whence);
    host_fd_ref_close(&host_ref);
    return ret < 0 ? linux_errno() : (int64_t) ret;
}

/* path operations. */

int64_t sys_readlinkat(guest_t *g,
                       int dirfd,
                       uint64_t path_gva,
                       uint64_t buf_gva,
                       uint64_t bufsiz)
{
    char path[LINUX_PATH_MAX];
    if (guest_read_str(g, path_gva, path, sizeof(path)) < 0)
        return -LINUX_EFAULT;
    char proc_path[LINUX_PATH_MAX];
    const char *intercept_path = path;
    int proc_resolved =
        resolve_proc_at_path(dirfd, path, proc_path, sizeof(proc_path));
    if (proc_resolved < 0)
        return -LINUX_ENAMETOOLONG;
    if (proc_resolved > 0)
        intercept_path = proc_path;

    /* Intercept /proc paths (e.g. /proc/self/exe, /proc/self/fd/N) */
    char link[LINUX_PATH_MAX];
    int intercepted =
        proc_intercept_readlink(intercept_path, link, sizeof(link));
    if (intercepted >= 0) {
        size_t copy_len =
            (size_t) intercepted < bufsiz ? (size_t) intercepted : bufsiz;
        if (guest_write(g, buf_gva, link, copy_len) < 0)
            return -LINUX_EFAULT;
        return (int64_t) copy_len;
    }
    if (intercepted == -1) {
        return linux_errno();
    }
    /* intercepted == PROC_NOT_INTERCEPTED: fall through */

    host_fd_ref_t dir_ref;
    if (host_dirfd_ref_open(dirfd, &dir_ref) < 0)
        return -LINUX_EBADF;

    /* Apply sysroot redirect for absolute paths */
    char sysroot_buf[LINUX_PATH_MAX];
    const char *read_path = path_resolve_sysroot_nofollow_path(
        path, sysroot_buf, sizeof(sysroot_buf));
    if (!read_path)
        return -LINUX_ENAMETOOLONG;
    ssize_t len = readlinkat(dir_ref.fd, read_path, link, sizeof(link) - 1);
    host_fd_ref_close(&dir_ref);
    if (len < 0)
        return linux_errno();

    size_t copy_len = (size_t) len < bufsiz ? (size_t) len : bufsiz;
    if (guest_write(g, buf_gva, link, copy_len) < 0)
        return -LINUX_EFAULT;

    return (int64_t) copy_len;
}

int64_t sys_unlinkat(guest_t *g, int dirfd, uint64_t path_gva, int flags)
{
    char path[LINUX_PATH_MAX];
    char proc_path[LINUX_PATH_MAX];
    const char *unlink_guest_path = path;
    if (guest_read_str(g, path_gva, path, sizeof(path)) < 0)
        return -LINUX_EFAULT;
    int proc_resolved =
        resolve_proc_at_path(dirfd, path, proc_path, sizeof(proc_path));
    if (proc_resolved < 0)
        return -LINUX_ENAMETOOLONG;
    if (proc_resolved > 0)
        unlink_guest_path = proc_path;

    if (!validate_at_flags(flags, LINUX_AT_REMOVEDIR))
        return -LINUX_EINVAL;

    host_fd_ref_t dir_ref;
    if (host_dirfd_ref_open(dirfd, &dir_ref) < 0)
        return -LINUX_EBADF;

    /* Rewrite /dev/shm/<name> to the host temp directory so shm_unlink works */
    const char *unlink_path;
    char shm_host[512];
    if (!strncmp(unlink_guest_path, "/dev/shm/", 9)) {
        const char *shm = proc_get_shm_dir();
        if (!shm) {
            host_fd_ref_close(&dir_ref);
            return linux_errno();
        }
        const char *suffix = unlink_guest_path + 9;
        if (strstr(suffix, "..") || strchr(suffix, '/') || suffix[0] == '\0') {
            host_fd_ref_close(&dir_ref);
            return -LINUX_EACCES;
        }
        int n = snprintf(shm_host, sizeof(shm_host), "%s/%s", shm, suffix);
        if (n < 0 || (size_t) n >= sizeof(shm_host)) {
            host_fd_ref_close(&dir_ref);
            return -LINUX_ENAMETOOLONG;
        }
        unlink_path = shm_host;
        host_fd_ref_close(&dir_ref);
        dir_ref.fd = AT_FDCWD;
        dir_ref.owned = 0;
    } else {
        char sysroot_buf[LINUX_PATH_MAX];
        const char *resolved = path_resolve_sysroot_create_path(
            unlink_guest_path, sysroot_buf, sizeof(sysroot_buf));
        if (!resolved) {
            host_fd_ref_close(&dir_ref);
            return -LINUX_ENAMETOOLONG;
        }
        unlink_path = resolved;
    }

    int host_flags = translate_at_flags(flags);
    if (unlinkat(dir_ref.fd, unlink_path, host_flags) < 0) {
        host_fd_ref_close(&dir_ref);
        return linux_errno();
    }

    host_fd_ref_close(&dir_ref);
    return 0;
}

int64_t sys_mkdirat(guest_t *g, int dirfd, uint64_t path_gva, int mode)
{
    char path[LINUX_PATH_MAX];
    char proc_path[LINUX_PATH_MAX];
    const char *mkdir_path = path;
    if (guest_read_str(g, path_gva, path, sizeof(path)) < 0)
        return -LINUX_EFAULT;
    int proc_resolved =
        resolve_proc_at_path(dirfd, path, proc_path, sizeof(proc_path));
    if (proc_resolved < 0)
        return -LINUX_ENAMETOOLONG;
    if (proc_resolved > 0)
        mkdir_path = proc_path;

    host_fd_ref_t dir_ref;
    if (host_dirfd_ref_open(dirfd, &dir_ref) < 0)
        return -LINUX_EBADF;

    char sysroot_buf[LINUX_PATH_MAX];
    const char *resolved = path_resolve_sysroot_create_path(
        mkdir_path, sysroot_buf, sizeof(sysroot_buf));
    if (!resolved) {
        host_fd_ref_close(&dir_ref);
        return -LINUX_ENAMETOOLONG;
    }

    if (mkdirat(dir_ref.fd, resolved, (mode_t) mode) < 0) {
        host_fd_ref_close(&dir_ref);
        return linux_errno();
    }

    host_fd_ref_close(&dir_ref);
    return 0;
}

static int64_t close_dir_refs_result(host_fd_ref_t *old_ref,
                                     host_fd_ref_t *new_ref,
                                     int64_t result)
{
    host_fd_ref_close(old_ref);
    host_fd_ref_close(new_ref);
    return result;
}

/* Linux RENAME_* flags for renameat2 */
#define LINUX_RENAME_NOREPLACE (1 << 0)
#define LINUX_RENAME_EXCHANGE (1 << 1)

int64_t sys_renameat2(guest_t *g,
                      int olddirfd,
                      uint64_t oldpath_gva,
                      int newdirfd,
                      uint64_t newpath_gva,
                      int flags)
{
    char oldpath[LINUX_PATH_MAX], newpath[LINUX_PATH_MAX];
    char old_proc_path[LINUX_PATH_MAX], new_proc_path[LINUX_PATH_MAX];
    const char *old_guest_path = oldpath;
    const char *new_guest_path = newpath;
    if (guest_read_str(g, oldpath_gva, oldpath, sizeof(oldpath)) < 0)
        return -LINUX_EFAULT;
    if (guest_read_str(g, newpath_gva, newpath, sizeof(newpath)) < 0)
        return -LINUX_EFAULT;
    int old_proc_resolved = resolve_proc_at_path(
        olddirfd, oldpath, old_proc_path, sizeof(old_proc_path));
    if (old_proc_resolved < 0)
        return -LINUX_ENAMETOOLONG;
    if (old_proc_resolved > 0)
        old_guest_path = old_proc_path;
    int new_proc_resolved = resolve_proc_at_path(
        newdirfd, newpath, new_proc_path, sizeof(new_proc_path));
    if (new_proc_resolved < 0)
        return -LINUX_ENAMETOOLONG;
    if (new_proc_resolved > 0)
        new_guest_path = new_proc_path;

    host_fd_ref_t olddir_ref, newdir_ref;
    if (host_dirfd_ref_open(olddirfd, &olddir_ref) < 0)
        return -LINUX_EBADF;
    if (host_dirfd_ref_open(newdirfd, &newdir_ref) < 0) {
        host_fd_ref_close(&olddir_ref);
        return -LINUX_EBADF;
    }

    if ((flags & ~(LINUX_RENAME_NOREPLACE | LINUX_RENAME_EXCHANGE)) ||
        ((flags & LINUX_RENAME_NOREPLACE) && (flags & LINUX_RENAME_EXCHANGE))) {
        return close_dir_refs_result(&olddir_ref, &newdir_ref, -LINUX_EINVAL);
    }

    /* Apply sysroot resolution for absolute paths */
    char old_sysroot[LINUX_PATH_MAX], new_sysroot[LINUX_PATH_MAX];
    const char *old_resolved = path_resolve_sysroot_path(
        old_guest_path, old_sysroot, sizeof(old_sysroot));
    const char *new_resolved = path_resolve_sysroot_create_path(
        new_guest_path, new_sysroot, sizeof(new_sysroot));
    if (!old_resolved || !new_resolved) {
        return close_dir_refs_result(&olddir_ref, &newdir_ref,
                                     -LINUX_ENAMETOOLONG);
    }

    /* RENAME_NOREPLACE: fail if destination exists. macOS renamex_np
     * supports RENAME_EXCL for the same semantics. Only supported for
     * AT_FDCWD paths (renamex_np does not take dirfd arguments).
     */
    if (flags & LINUX_RENAME_NOREPLACE) {
        if (olddirfd == LINUX_AT_FDCWD && newdirfd == LINUX_AT_FDCWD) {
            if (renamex_np(old_resolved, new_resolved, RENAME_EXCL) < 0) {
                return close_dir_refs_result(&olddir_ref, &newdir_ref,
                                             linux_errno());
            }
            return close_dir_refs_result(&olddir_ref, &newdir_ref, 0);
        }
        /* For non-CWD dirfds, emulate with link+unlink. This is not atomic,
         * but linkat() still preserves the "destination must not exist"
         * requirement. This path still cannot handle directories because
         * hardlinking directories is not allowed.
         */
        if (linkat(olddir_ref.fd, old_resolved, newdir_ref.fd, new_resolved,
                   0) < 0) {
            return close_dir_refs_result(&olddir_ref, &newdir_ref,
                                         linux_errno());
        }
        if (unlinkat(olddir_ref.fd, old_resolved, 0) < 0) {
            int err = errno;
            (void) unlinkat(newdir_ref.fd, new_resolved, 0);
            errno = err;
            return close_dir_refs_result(&olddir_ref, &newdir_ref,
                                         linux_errno());
        }
        return close_dir_refs_result(&olddir_ref, &newdir_ref, 0);
    }

    /* RENAME_EXCHANGE: swap two paths. macOS renamex_np supports RENAME_SWAP.
     */
    if (flags & LINUX_RENAME_EXCHANGE) {
        if (olddirfd == LINUX_AT_FDCWD && newdirfd == LINUX_AT_FDCWD) {
            if (renamex_np(old_resolved, new_resolved, RENAME_SWAP) < 0) {
                return close_dir_refs_result(&olddir_ref, &newdir_ref,
                                             linux_errno());
            }
            return close_dir_refs_result(&olddir_ref, &newdir_ref, 0);
        }
        return close_dir_refs_result(
            &olddir_ref, &newdir_ref,
            -LINUX_EINVAL); /* RENAME_EXCHANGE requires AT_FDCWD on macOS */
    }

    if (olddirfd == LINUX_AT_FDCWD && newdirfd == LINUX_AT_FDCWD) {
        if (rename(old_resolved, new_resolved) < 0) {
            return close_dir_refs_result(&olddir_ref, &newdir_ref,
                                         linux_errno());
        }
        return close_dir_refs_result(&olddir_ref, &newdir_ref, 0);
    }

    if (renameat(olddir_ref.fd, old_resolved, newdir_ref.fd, new_resolved) <
        0) {
        return close_dir_refs_result(&olddir_ref, &newdir_ref, linux_errno());
    }
    return close_dir_refs_result(&olddir_ref, &newdir_ref, 0);
}

int64_t sys_mknodat(guest_t *g, int dirfd, uint64_t path_gva, int mode, int dev)
{
    (void) dev;
    char path[LINUX_PATH_MAX];
    char proc_path[LINUX_PATH_MAX];
    const char *node_path = path;
    if (guest_read_str(g, path_gva, path, sizeof(path)) < 0)
        return -LINUX_EFAULT;
    int proc_resolved =
        resolve_proc_at_path(dirfd, path, proc_path, sizeof(proc_path));
    if (proc_resolved < 0)
        return -LINUX_ENAMETOOLONG;
    if (proc_resolved > 0)
        node_path = proc_path;

    host_fd_ref_t dir_ref;
    if (host_dirfd_ref_open(dirfd, &dir_ref) < 0)
        return -LINUX_EBADF;

    /* Only support FIFO creation; other node types need root */
    if (S_ISFIFO(mode)) {
        if (mkfifoat(dir_ref.fd, node_path, mode & 0777) < 0) {
            host_fd_ref_close(&dir_ref);
            return linux_errno();
        }
        host_fd_ref_close(&dir_ref);
        return 0;
    }

    /* Regular files: create an empty file */
    if (S_ISREG(mode) || (mode & S_IFMT) == 0) {
        int fd = openat(dir_ref.fd, node_path, O_CREAT | O_WRONLY | O_EXCL,
                        mode & 0777);
        host_fd_ref_close(&dir_ref);
        if (fd < 0)
            return linux_errno();
        close(fd);
        return 0;
    }

    host_fd_ref_close(&dir_ref);
    return -LINUX_ENOSYS;
}

int64_t sys_symlinkat(guest_t *g,
                      uint64_t target_gva,
                      int dirfd,
                      uint64_t linkpath_gva)
{
    char target[LINUX_PATH_MAX], linkpath[LINUX_PATH_MAX];
    char proc_path[LINUX_PATH_MAX];
    const char *link_guest_path = linkpath;
    if (guest_read_str(g, target_gva, target, sizeof(target)) < 0)
        return -LINUX_EFAULT;
    if (guest_read_str(g, linkpath_gva, linkpath, sizeof(linkpath)) < 0)
        return -LINUX_EFAULT;
    int proc_resolved =
        resolve_proc_at_path(dirfd, linkpath, proc_path, sizeof(proc_path));
    if (proc_resolved < 0)
        return -LINUX_ENAMETOOLONG;
    if (proc_resolved > 0)
        link_guest_path = proc_path;

    host_fd_ref_t dir_ref;
    if (host_dirfd_ref_open(dirfd, &dir_ref) < 0)
        return -LINUX_EBADF;

    /* Resolve linkpath (the new symlink location) through sysroot */
    char sysroot_buf[LINUX_PATH_MAX];
    const char *resolved = path_resolve_sysroot_create_path(
        link_guest_path, sysroot_buf, sizeof(sysroot_buf));
    if (!resolved) {
        host_fd_ref_close(&dir_ref);
        return -LINUX_ENAMETOOLONG;
    }

    if (symlinkat(target, dir_ref.fd, resolved) < 0) {
        host_fd_ref_close(&dir_ref);
        return linux_errno();
    }

    host_fd_ref_close(&dir_ref);
    return 0;
}

int64_t sys_linkat(guest_t *g,
                   int olddirfd,
                   uint64_t oldpath_gva,
                   int newdirfd,
                   uint64_t newpath_gva,
                   int flags)
{
    char oldpath[LINUX_PATH_MAX], newpath[LINUX_PATH_MAX];
    char old_proc_path[LINUX_PATH_MAX], new_proc_path[LINUX_PATH_MAX];
    const char *old_guest_path = oldpath;
    const char *new_guest_path = newpath;
    if (guest_read_str(g, oldpath_gva, oldpath, sizeof(oldpath)) < 0)
        return -LINUX_EFAULT;
    if (guest_read_str(g, newpath_gva, newpath, sizeof(newpath)) < 0)
        return -LINUX_EFAULT;
    int old_proc_resolved = resolve_proc_at_path(
        olddirfd, oldpath, old_proc_path, sizeof(old_proc_path));
    if (old_proc_resolved < 0)
        return -LINUX_ENAMETOOLONG;
    if (old_proc_resolved > 0)
        old_guest_path = old_proc_path;
    int new_proc_resolved = resolve_proc_at_path(
        newdirfd, newpath, new_proc_path, sizeof(new_proc_path));
    if (new_proc_resolved < 0)
        return -LINUX_ENAMETOOLONG;
    if (new_proc_resolved > 0)
        new_guest_path = new_proc_path;

    if (!validate_at_flags(flags, LINUX_AT_SYMLINK_FOLLOW))
        return -LINUX_EINVAL;

    host_fd_ref_t olddir_ref, newdir_ref;
    if (host_dirfd_ref_open(olddirfd, &olddir_ref) < 0)
        return -LINUX_EBADF;
    if (host_dirfd_ref_open(newdirfd, &newdir_ref) < 0) {
        host_fd_ref_close(&olddir_ref);
        return -LINUX_EBADF;
    }

    /* Resolve both paths through sysroot */
    char old_sr[LINUX_PATH_MAX], new_sr[LINUX_PATH_MAX];
    const char *old_resolved =
        path_resolve_sysroot_path(old_guest_path, old_sr, sizeof(old_sr));
    const char *new_resolved = path_resolve_sysroot_create_path(
        new_guest_path, new_sr, sizeof(new_sr));
    if (!old_resolved || !new_resolved) {
        host_fd_ref_close(&olddir_ref);
        host_fd_ref_close(&newdir_ref);
        return -LINUX_ENAMETOOLONG;
    }

    int mac_flags = translate_at_flags(flags);
    if (linkat(olddir_ref.fd, old_resolved, newdir_ref.fd, new_resolved,
               mac_flags) < 0) {
        host_fd_ref_close(&olddir_ref);
        host_fd_ref_close(&newdir_ref);
        return linux_errno();
    }

    host_fd_ref_close(&olddir_ref);
    host_fd_ref_close(&newdir_ref);
    return 0;
}

int64_t sys_faccessat(guest_t *g,
                      int dirfd,
                      uint64_t path_gva,
                      int mode,
                      int flags)
{
    if (dirfd == LINUX_AT_FDCWD) {
        char dot_path[2];
        if (guest_read_small(g, path_gva, dot_path, sizeof(dot_path)) == 0 &&
            dot_path[0] == '.' && dot_path[1] == '\0') {
            int mac_flags = translate_faccessat_flags(flags);
            if (faccessat(AT_FDCWD, ".", mode, mac_flags) < 0)
                return linux_errno();
            return 0;
        }
    }

    char path[LINUX_PATH_MAX];
    char proc_path[LINUX_PATH_MAX];
    const char *access_path = path;
    if (guest_read_str(g, path_gva, path, sizeof(path)) < 0)
        return -LINUX_EFAULT;
    int proc_resolved =
        resolve_proc_at_path(dirfd, path, proc_path, sizeof(proc_path));
    if (proc_resolved < 0)
        return -LINUX_ENAMETOOLONG;
    if (proc_resolved > 0)
        access_path = proc_path;

    if (!validate_at_flags(flags, LINUX_AT_EACCESS | LINUX_AT_SYMLINK_NOFOLLOW))
        return -LINUX_EINVAL;

    if (proc_resolved == 0 && dirfd == LINUX_AT_FDCWD && path[0] != '/') {
        int mac_flags = translate_faccessat_flags(flags);
        if (faccessat(AT_FDCWD, path, mode, mac_flags) < 0)
            return linux_errno();
        return 0;
    }

    host_fd_ref_t dir_ref;
    if (host_dirfd_ref_open(dirfd, &dir_ref) < 0)
        return -LINUX_EBADF;

    /* Check intercepted stat paths first since macOS has no /proc filesystem
     * and the sysfs CPU tree is synthetic. Access must reflect the synthetic
     * mode bits, not just path existence.
     */
    struct stat intercepted_st;
    if (path_might_use_stat_intercept(access_path) &&
        proc_intercept_stat(access_path, &intercepted_st) == 0) {
        host_fd_ref_close(&dir_ref);
        if (path_check_intercept_access(&intercepted_st, mode, flags) < 0)
            return linux_errno();
        return 0;
    }

    char sysroot_buf[LINUX_PATH_MAX];
    const char *check_path =
        (flags & LINUX_AT_SYMLINK_NOFOLLOW)
            ? path_resolve_sysroot_nofollow_path(access_path, sysroot_buf,
                                                 sizeof(sysroot_buf))
            : path_resolve_sysroot_path(access_path, sysroot_buf,
                                        sizeof(sysroot_buf));
    if (!check_path) {
        host_fd_ref_close(&dir_ref);
        return -LINUX_ENAMETOOLONG;
    }

    int mac_flags = translate_faccessat_flags(flags);
    if (faccessat(dir_ref.fd, check_path, mode, mac_flags) < 0) {
        host_fd_ref_close(&dir_ref);
        return linux_errno();
    }

    host_fd_ref_close(&dir_ref);
    return 0;
}

/* truncate. */

int64_t sys_ftruncate(int fd, int64_t length)
{
    fd_entry_t snap;
    if (!fd_snapshot(fd, &snap))
        return -LINUX_EBADF;
    /* Linux: ftruncate on an O_PATH fd returns EBADF. */
    if (snap.type == FD_PATH)
        return -LINUX_EBADF;

    /* Enforce memfd seals on truncate. */
    int seals = snap.seals;
    if (seals & LINUX_F_SEAL_WRITE)
        return -LINUX_EPERM;

    host_fd_ref_t host_ref;
    if (host_fd_ref_open(fd, &host_ref) < 0)
        return -LINUX_EBADF;

    if (seals & (LINUX_F_SEAL_SHRINK | LINUX_F_SEAL_GROW)) {
        struct stat st;
        if (fstat(host_ref.fd, &st) == 0) {
            if ((seals & LINUX_F_SEAL_SHRINK) && length < st.st_size) {
                host_fd_ref_close(&host_ref);
                return -LINUX_EPERM;
            }
            if ((seals & LINUX_F_SEAL_GROW) && length > st.st_size) {
                host_fd_ref_close(&host_ref);
                return -LINUX_EPERM;
            }
        }
    }

    if (ftruncate(host_ref.fd, length) < 0) {
        host_fd_ref_close(&host_ref);
        return linux_errno();
    }
    host_fd_ref_close(&host_ref);
    return 0;
}

int64_t sys_truncate(guest_t *g, uint64_t path_gva, int64_t length)
{
    char path[LINUX_PATH_MAX];
    char proc_path[LINUX_PATH_MAX];
    const char *trunc_guest_path = path;
    if (guest_read_str(g, path_gva, path, sizeof(path)) < 0)
        return -LINUX_EFAULT;
    int proc_resolved = resolve_proc_at_path(LINUX_AT_FDCWD, path, proc_path,
                                             sizeof(proc_path));
    if (proc_resolved < 0)
        return -LINUX_ENAMETOOLONG;
    if (proc_resolved > 0)
        trunc_guest_path = proc_path;

    char sysroot_buf[LINUX_PATH_MAX];
    const char *trunc_path = path_resolve_sysroot_path(
        trunc_guest_path, sysroot_buf, sizeof(sysroot_buf));
    if (!trunc_path)
        return -LINUX_ENAMETOOLONG;

    if (truncate(trunc_path, length) < 0)
        return linux_errno();
    return 0;
}

/* permissions/ownership. */

int64_t sys_fchmod(int fd, uint32_t mode)
{
    /* O_PATH fds do not support fchmod (Linux returns EBADF) */
    fd_entry_t snap;
    if (fd_snapshot(fd, &snap) && snap.type == FD_PATH)
        return -LINUX_EBADF;
    host_fd_ref_t host_ref;
    if (host_fd_ref_open(fd, &host_ref) < 0)
        return -LINUX_EBADF;
    if (fchmod(host_ref.fd, mode) < 0) {
        host_fd_ref_close(&host_ref);
        return linux_errno();
    }
    host_fd_ref_close(&host_ref);
    return 0;
}

int64_t sys_fchmodat(guest_t *g,
                     int dirfd,
                     uint64_t path_gva,
                     uint32_t mode,
                     int flags)
{
    char path[LINUX_PATH_MAX];
    char proc_path[LINUX_PATH_MAX];
    const char *chmod_path = path;
    if (guest_read_str(g, path_gva, path, sizeof(path)) < 0)
        return -LINUX_EFAULT;
    int proc_resolved =
        resolve_proc_at_path(dirfd, path, proc_path, sizeof(proc_path));
    if (proc_resolved < 0)
        return -LINUX_ENAMETOOLONG;
    if (proc_resolved > 0)
        chmod_path = proc_path;

    if (!validate_at_flags(flags, LINUX_AT_SYMLINK_NOFOLLOW))
        return -LINUX_EINVAL;

    host_fd_ref_t dir_ref;
    if (host_dirfd_ref_open(dirfd, &dir_ref) < 0)
        return -LINUX_EBADF;

    int mac_flags = translate_at_flags(flags);
    if (fchmodat(dir_ref.fd, chmod_path, mode, mac_flags) < 0) {
        host_fd_ref_close(&dir_ref);
        return linux_errno();
    }

    host_fd_ref_close(&dir_ref);
    return 0;
}

int64_t sys_fchownat(guest_t *g,
                     int dirfd,
                     uint64_t path_gva,
                     uint32_t owner,
                     uint32_t group,
                     int flags)
{
    char path[LINUX_PATH_MAX];
    char proc_path[LINUX_PATH_MAX];
    const char *chown_path = path;
    if (guest_read_str(g, path_gva, path, sizeof(path)) < 0)
        return -LINUX_EFAULT;
    int proc_resolved =
        resolve_proc_at_path(dirfd, path, proc_path, sizeof(proc_path));
    if (proc_resolved < 0)
        return -LINUX_ENAMETOOLONG;
    if (proc_resolved > 0)
        chown_path = proc_path;

    if (!validate_at_flags(flags, LINUX_AT_SYMLINK_NOFOLLOW))
        return -LINUX_EINVAL;

    host_fd_ref_t dir_ref;
    if (host_dirfd_ref_open(dirfd, &dir_ref) < 0)
        return -LINUX_EBADF;

    int mac_flags = translate_at_flags(flags);
    if (fchownat(dir_ref.fd, chown_path, owner, group, mac_flags) < 0) {
        host_fd_ref_close(&dir_ref);
        return linux_errno();
    }

    host_fd_ref_close(&dir_ref);
    return 0;
}

int64_t sys_fchown(int fd, uint32_t owner, uint32_t group)
{
    /* O_PATH fds do not support fchown (Linux returns EBADF) */
    fd_entry_t snap;
    if (fd_snapshot(fd, &snap) && snap.type == FD_PATH)
        return -LINUX_EBADF;
    host_fd_ref_t host_ref;
    if (host_fd_ref_open(fd, &host_ref) < 0)
        return -LINUX_EBADF;
    if (fchown(host_ref.fd, owner, group) < 0) {
        host_fd_ref_close(&host_ref);
        return linux_errno();
    }
    host_fd_ref_close(&host_ref);
    return 0;
}

int64_t sys_utimensat(guest_t *g,
                      int dirfd,
                      uint64_t path_gva,
                      uint64_t times_gva,
                      int flags)
{
    if (!validate_at_flags(flags,
                           LINUX_AT_SYMLINK_NOFOLLOW | LINUX_AT_EMPTY_PATH))
        return -LINUX_EINVAL;

    host_fd_ref_t dir_ref;
    if (host_dirfd_ref_open(dirfd, &dir_ref) < 0)
        return -LINUX_EBADF;

    /* If path is NULL (path_gva == 0), operate on the dirfd itself */
    const char *path_arg = NULL;
    char path[LINUX_PATH_MAX];
    char proc_path[LINUX_PATH_MAX];
    if (path_gva != 0) {
        if (guest_read_str(g, path_gva, path, sizeof(path)) < 0) {
            host_fd_ref_close(&dir_ref);
            return -LINUX_EFAULT;
        }
        int proc_resolved =
            resolve_proc_at_path(dirfd, path, proc_path, sizeof(proc_path));
        if (proc_resolved < 0) {
            host_fd_ref_close(&dir_ref);
            return -LINUX_ENAMETOOLONG;
        }
        path_arg = proc_resolved > 0 ? proc_path : path;
    }

    struct timespec ts[2];
    if (times_gva != 0) {
        /* Read two linux_timespec_t from guest */
        linux_timespec_t lts[2];
        if (guest_read_small(g, times_gva, lts, sizeof(lts)) < 0) {
            host_fd_ref_close(&dir_ref);
            return -LINUX_EFAULT;
        }

        /* UTIME_NOW = 0x3FFFFFFF, UTIME_OMIT = 0x3FFFFFFE (same on macOS) */
        ts[0].tv_sec = lts[0].tv_sec;
        ts[0].tv_nsec = lts[0].tv_nsec;
        ts[1].tv_sec = lts[1].tv_sec;
        ts[1].tv_nsec = lts[1].tv_nsec;
    }

    int mac_flags = 0;
    if (flags & LINUX_AT_SYMLINK_NOFOLLOW)
        mac_flags |= AT_SYMLINK_NOFOLLOW;

    /* macOS utimensat() does not support NULL path (Linux extension).
     * When path is NULL, the caller wants to operate on dirfd itself,
     * so use futimens() instead.
     */
    if (!path_arg) {
        if (futimens(dir_ref.fd, times_gva ? ts : NULL) < 0) {
            host_fd_ref_close(&dir_ref);
            return linux_errno();
        }
    } else {
        if (utimensat(dir_ref.fd, path_arg, times_gva ? ts : NULL, mac_flags) <
            0) {
            host_fd_ref_close(&dir_ref);
            return linux_errno();
        }
    }

    host_fd_ref_close(&dir_ref);
    return 0;
}
