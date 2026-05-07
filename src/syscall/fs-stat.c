/* Filesystem stat/statx/statfs handlers
 *
 * Copyright 2026 elfuse contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdbool.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/stat.h>

#include "debug/log.h"

#include "runtime/procemu.h"

#include "syscall/abi.h"
#include "syscall/fs.h"
#include "syscall/internal.h"
#include "syscall/path.h"
#include "syscall/proc.h"

static uint64_t mac_to_linux_dev(dev_t dev)
{
    unsigned int maj = ((unsigned int) dev >> 24) & 0xFF;
    unsigned int min = (unsigned int) dev & 0xFFFFFF;
    return (uint64_t) ((min & 0xFF) | (maj << 8) | ((min & ~0xFFU) << 12));
}

static uint32_t linux_dev_major(uint64_t dev)
{
    return (uint32_t) (((dev & 0x00000000000FFF00ULL) >> 8) |
                       ((dev & 0xFFFFF00000000000ULL) >> 32));
}

static uint32_t linux_dev_minor(uint64_t dev)
{
    return (uint32_t) ((dev & 0x00000000000000FFULL) |
                       ((dev & 0x00000FFFFFF00000ULL) >> 12));
}

static void translate_stat(const struct stat *mac, linux_stat_t *lin)
{
    memset(lin, 0, sizeof(*lin));
    lin->st_dev = mac_to_linux_dev(mac->st_dev);
    lin->st_ino = mac->st_ino;
    lin->st_mode = mac->st_mode;
    lin->st_nlink = (uint32_t) mac->st_nlink;
    lin->st_uid = mac->st_uid;
    lin->st_gid = mac->st_gid;
    lin->st_rdev = mac_to_linux_dev(mac->st_rdev);
    lin->st_size = mac->st_size;
    lin->st_blksize = (int32_t) mac->st_blksize;
    lin->st_blocks = mac->st_blocks;
    lin->st_atime_sec = mac->st_atimespec.tv_sec;
    lin->st_atime_nsec = mac->st_atimespec.tv_nsec;
    lin->st_mtime_sec = mac->st_mtimespec.tv_sec;
    lin->st_mtime_nsec = mac->st_mtimespec.tv_nsec;
    lin->st_ctime_sec = mac->st_ctimespec.tv_sec;
    lin->st_ctime_nsec = mac->st_ctimespec.tv_nsec;
}

static uint32_t normalize_statx_mask(unsigned int requested_mask)
{
    uint32_t mask = requested_mask & (STATX_BASIC_STATS | STATX_BTIME);
    if (mask == 0)
        mask = STATX_BASIC_STATS;
    return mask;
}

static void translate_statx(const struct stat *mac,
                            linux_statx_t *sx,
                            uint32_t mask)
{
    uint64_t linux_rdev = mac_to_linux_dev(mac->st_rdev);
    uint64_t linux_dev = mac_to_linux_dev(mac->st_dev);

    memset(sx, 0, sizeof(*sx));
    sx->stx_mask = mask;
    sx->stx_blksize = (uint32_t) mac->st_blksize;

    if (mask & (STATX_TYPE | STATX_MODE))
        sx->stx_mode = (uint16_t) mac->st_mode;
    if (mask & STATX_NLINK)
        sx->stx_nlink = (uint32_t) mac->st_nlink;
    if (mask & STATX_UID)
        sx->stx_uid = mac->st_uid;
    if (mask & STATX_GID)
        sx->stx_gid = mac->st_gid;
    if (mask & STATX_INO)
        sx->stx_ino = mac->st_ino;
    if (mask & STATX_SIZE)
        sx->stx_size = mac->st_size;
    if (mask & STATX_BLOCKS)
        sx->stx_blocks = mac->st_blocks;
    if (mask & STATX_ATIME) {
        sx->stx_atime_sec = mac->st_atimespec.tv_sec;
        sx->stx_atime_nsec = (uint32_t) mac->st_atimespec.tv_nsec;
    }
    if (mask & STATX_MTIME) {
        sx->stx_mtime_sec = mac->st_mtimespec.tv_sec;
        sx->stx_mtime_nsec = (uint32_t) mac->st_mtimespec.tv_nsec;
    }
    if (mask & STATX_CTIME) {
        sx->stx_ctime_sec = mac->st_ctimespec.tv_sec;
        sx->stx_ctime_nsec = (uint32_t) mac->st_ctimespec.tv_nsec;
    }
    if (mask & STATX_BTIME) {
        sx->stx_btime_sec = mac->st_birthtimespec.tv_sec;
        sx->stx_btime_nsec = (uint32_t) mac->st_birthtimespec.tv_nsec;
    }

    sx->stx_rdev_major = linux_dev_major(linux_rdev);
    sx->stx_rdev_minor = linux_dev_minor(linux_rdev);
    sx->stx_dev_major = linux_dev_major(linux_dev);
    sx->stx_dev_minor = linux_dev_minor(linux_dev);
}

static int write_linux_stat(guest_t *g,
                            uint64_t stat_gva,
                            const struct stat *mac_st)
{
    linux_stat_t lin_st;

    translate_stat(mac_st, &lin_st);
    return guest_write_small(g, stat_gva, &lin_st, sizeof(lin_st));
}

static int write_linux_statx(guest_t *g,
                             uint64_t statx_gva,
                             const struct stat *mac_st,
                             unsigned int mask)
{
    linux_statx_t sx;

    translate_statx(mac_st, &sx, normalize_statx_mask(mask));
    return guest_write_small(g, statx_gva, &sx, sizeof(sx));
}

static void translate_statfs(const struct statfs *mac, linux_statfs_t *lin)
{
    memset(lin, 0, sizeof(*lin));
    lin->f_type = mac->f_type;
    lin->f_bsize = mac->f_bsize;
    lin->f_blocks = mac->f_blocks;
    lin->f_bfree = mac->f_bfree;
    lin->f_bavail = mac->f_bavail;
    lin->f_files = mac->f_files;
    lin->f_ffree = mac->f_ffree;
    lin->f_fsid[0] = mac->f_fsid.val[0];
    lin->f_fsid[1] = mac->f_fsid.val[1];
    lin->f_namelen = 255;
    lin->f_frsize = mac->f_bsize;
}

/* Resolve the directory + path arguments of a *at-style stat operation and
 * fill *mac_st via the appropriate host call (proc intercept where applicable).
 * Shared by sys_newfstatat and sys_statx; the caller copies the result into
 * the guest's struct stat or struct statx layout. Returns 0 on success or a
 * negative Linux errno.
 */
static int64_t stat_at_path(guest_t *g,
                            int dirfd,
                            uint64_t path_gva,
                            int flags,
                            struct stat *mac_st)
{
    if (dirfd == LINUX_AT_FDCWD) {
        char dot_path[2];
        if (guest_read_small(g, path_gva, dot_path, sizeof(dot_path)) == 0 &&
            dot_path[0] == '.' && dot_path[1] == '\0') {
            int mac_flags = translate_at_flags(flags);
            if (fstatat(AT_FDCWD, ".", mac_st, mac_flags) < 0)
                return linux_errno();
            return 0;
        }
    }

    char short_path[64];
    char path[LINUX_PATH_MAX];
    const char *pathp;
    if (guest_read_path(g, path_gva, short_path, sizeof(short_path), path,
                        sizeof(path), &pathp) < 0)
        return -LINUX_EFAULT;

    char proc_path[LINUX_PATH_MAX];
    const char *intercept_path = pathp;
    int proc_resolved =
        resolve_proc_at_path(dirfd, pathp, proc_path, sizeof(proc_path));
    if (proc_resolved < 0)
        return -LINUX_ENAMETOOLONG;
    if (proc_resolved > 0)
        intercept_path = proc_path;

    if (proc_resolved == 0 && dirfd == LINUX_AT_FDCWD && pathp[0] != '/' &&
        pathp[0] != '\0' && !proc_get_sysroot()) {
        int mac_flags = translate_at_flags(flags);
        if (fstatat(AT_FDCWD, pathp, mac_st, mac_flags) < 0)
            return linux_errno();
        return 0;
    }

    host_fd_ref_t dir_ref;
    if (host_dirfd_ref_open(dirfd, &dir_ref) < 0)
        return -LINUX_EBADF;

    int64_t rc = 0;
    if ((flags & LINUX_AT_EMPTY_PATH) && pathp[0] == '\0') {
        if (dir_ref.fd < 0 || dir_ref.fd == AT_FDCWD) {
            rc = -LINUX_EBADF;
            goto done;
        }
        if (fstat(dir_ref.fd, mac_st) < 0) {
            rc = linux_errno();
            goto done;
        }
    } else {
        int intercepted = PROC_NOT_INTERCEPTED;
        if (path_might_use_stat_intercept(intercept_path)) {
            intercepted = proc_intercept_stat(intercept_path, mac_st);
            if (intercepted == -1) {
                rc = linux_errno();
                goto done;
            }
        }
        if (intercepted == PROC_NOT_INTERCEPTED) {
            char sysroot_buf[LINUX_PATH_MAX];
            const char *stat_path =
                (flags & LINUX_AT_SYMLINK_NOFOLLOW)
                    ? path_resolve_sysroot_nofollow_path(pathp, sysroot_buf,
                                                         sizeof(sysroot_buf))
                    : path_resolve_sysroot_path(pathp, sysroot_buf,
                                                sizeof(sysroot_buf));
            if (!stat_path) {
                rc = -LINUX_ENAMETOOLONG;
                goto done;
            }
            int mac_flags = translate_at_flags(flags);
            if (fstatat(dir_ref.fd, stat_path, mac_st, mac_flags) < 0) {
                rc = linux_errno();
                goto done;
            }
        }
    }

done:
    host_fd_ref_close(&dir_ref);
    return rc;
}

int64_t sys_fstat(guest_t *g, int fd, uint64_t stat_gva)
{
    host_fd_ref_t host_ref;
    if (host_fd_ref_open(fd, &host_ref) < 0) {
        log_debug("fstat(%d): invalid guest fd", fd);
        return -LINUX_EBADF;
    }

    struct stat mac_st;
    if (fstat(host_ref.fd, &mac_st) < 0) {
        log_debug("fstat(%d->%d): host fstat failed errno=%d", fd, host_ref.fd,
                  errno);
        host_fd_ref_close(&host_ref);
        return linux_errno();
    }

    if (write_linux_stat(g, stat_gva, &mac_st) < 0) {
        host_fd_ref_close(&host_ref);
        return -LINUX_EFAULT;
    }

    host_fd_ref_close(&host_ref);
    return 0;
}

int64_t sys_newfstatat(guest_t *g,
                       int dirfd,
                       uint64_t path_gva,
                       uint64_t stat_gva,
                       int flags)
{
    if (!validate_at_flags(flags, LINUX_AT_SYMLINK_NOFOLLOW |
                                      LINUX_AT_EMPTY_PATH |
                                      LINUX_AT_NO_AUTOMOUNT))
        return -LINUX_EINVAL;

    struct stat mac_st;
    int64_t rc = stat_at_path(g, dirfd, path_gva, flags, &mac_st);
    if (rc < 0)
        return rc;

    if (write_linux_stat(g, stat_gva, &mac_st) < 0)
        return -LINUX_EFAULT;
    return 0;
}

int64_t sys_statfs(guest_t *g, uint64_t path_gva, uint64_t buf_gva)
{
    char path[LINUX_PATH_MAX];
    if (guest_read_str_small(g, path_gva, path, sizeof(path)) < 0)
        return -LINUX_EFAULT;

    char sysroot_buf[LINUX_PATH_MAX];
    const char *fs_path =
        path_resolve_sysroot_path(path, sysroot_buf, sizeof(sysroot_buf));
    if (!fs_path)
        return -LINUX_ENAMETOOLONG;

    struct statfs mac_st;
    if (statfs(fs_path, &mac_st) < 0)
        return linux_errno();

    linux_statfs_t lin_st;
    translate_statfs(&mac_st, &lin_st);
    if (guest_write_small(g, buf_gva, &lin_st, sizeof(lin_st)) < 0)
        return -LINUX_EFAULT;

    return 0;
}

int64_t sys_fstatfs(guest_t *g, int fd, uint64_t buf_gva)
{
    host_fd_ref_t host_ref;
    if (host_fd_ref_open(fd, &host_ref) < 0)
        return -LINUX_EBADF;

    struct statfs mac_st;
    if (fstatfs(host_ref.fd, &mac_st) < 0) {
        host_fd_ref_close(&host_ref);
        return linux_errno();
    }

    linux_statfs_t lin_st;
    translate_statfs(&mac_st, &lin_st);
    if (guest_write_small(g, buf_gva, &lin_st, sizeof(lin_st)) < 0) {
        host_fd_ref_close(&host_ref);
        return -LINUX_EFAULT;
    }

    host_fd_ref_close(&host_ref);
    return 0;
}

int64_t sys_statx(guest_t *g,
                  int dirfd,
                  uint64_t path_gva,
                  int flags,
                  unsigned int mask,
                  uint64_t statxbuf_gva)
{
    if (!validate_at_flags(
            flags, LINUX_AT_SYMLINK_NOFOLLOW | LINUX_AT_EMPTY_PATH |
                       LINUX_AT_NO_AUTOMOUNT | LINUX_AT_STATX_SYNC_TYPE))
        return -LINUX_EINVAL;

    struct stat mac_st;
    int64_t rc = stat_at_path(g, dirfd, path_gva, flags, &mac_st);
    if (rc < 0)
        return rc;

    if (write_linux_statx(g, statxbuf_gva, &mac_st, mask) < 0)
        return -LINUX_EFAULT;
    return 0;
}
