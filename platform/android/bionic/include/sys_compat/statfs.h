#pragma once
#include <sys/param.h>
#include <sys/mount.h>
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>
#include <sys_compat/muplar_errno.h>

/* -----------------------
 * Linux statfs structure
 * ----------------------- */
struct linux_statfs {
    unsigned long f_type;
    unsigned long f_bsize;
    unsigned long f_blocks;
    unsigned long f_bfree;
    unsigned long f_bavail;
    unsigned long f_files;
    unsigned long f_ffree;
    struct { int val[2]; } f_fsid;  // Linux expects array of 2 ints
    unsigned long f_namelen;
    unsigned long f_frsize;
    unsigned long f_flags;
    unsigned long f_spare[4];
};

/* -----------------------
 * Path-based statfs wrapper
 * ----------------------- */
static inline int linux_statfs(const char *path, struct linux_statfs *buf) {
    struct statfs st;

    if (statfs(path, &st) != 0)
        return -1;

    buf->f_type   = (uint32_t)st.f_type;
    buf->f_bsize  = (uint32_t)st.f_bsize;
    buf->f_blocks = st.f_blocks;
    buf->f_bfree  = st.f_bfree;
    buf->f_bavail = st.f_bavail;
    buf->f_files  = st.f_files;
    buf->f_ffree  = st.f_ffree;

    /* Correctly copy fsid_t (macOS) to Linux f_fsid */
    buf->f_fsid.val[0] = st.f_fsid.val[0];
    buf->f_fsid.val[1] = st.f_fsid.val[1];

    /* Linux-only fields, set defaults */
    buf->f_namelen = 1024;       /* macOS has no f_namelen */
    buf->f_frsize  = st.f_bsize; /* macOS has no f_frsize */

    return 0;
}

/* Convert macOS statfs -> Linux statfs */
static inline void statfs_to_linux(const struct statfs *s, struct linux_statfs *ls) {
    ls->f_type   = s->f_type;
    ls->f_bsize  = s->f_bsize;
    ls->f_blocks = s->f_blocks;
    ls->f_bfree  = s->f_bfree;
    ls->f_bavail = s->f_bavail;
    ls->f_files  = s->f_files;
    ls->f_ffree  = s->f_ffree;

    // Convert macOS fsid_t to Linux f_fsid
    ls->f_fsid.val[0] = s->f_fsid.val[0];
    ls->f_fsid.val[1] = s->f_fsid.val[1];

    // Linux-only fields — assign reasonable defaults
    ls->f_namelen = 255;        // typical Linux max filename
    ls->f_frsize  = s->f_bsize; // no separate frsize on macOS
    ls->f_flags   = s->f_flags; // copy mount flags
    for (int i = 0; i < 4; i++) ls->f_spare[i] = 0;
}

/* Convert Linux statfs -> macOS statfs */
static inline void linux_to_statfs(const struct linux_statfs *ls, struct statfs *s) {
    s->f_type   = (uint32_t)ls->f_type;
    s->f_bsize  = (uint32_t)ls->f_bsize;
    s->f_blocks = ls->f_blocks;
    s->f_bfree  = ls->f_bfree;
    s->f_bavail = ls->f_bavail;
    s->f_files  = ls->f_files;
    s->f_ffree  = ls->f_ffree;

    // Convert Linux f_fsid to macOS fsid_t
    s->f_fsid.val[0] = ls->f_fsid.val[0];
    s->f_fsid.val[1] = ls->f_fsid.val[1];

    // f_flags exists on both
    s->f_flags = (uint32_t)ls->f_flags;

    // macOS-specific fields: set to defaults or empty strings
    // fs type name, mount point, mounted from
    snprintf(s->f_fstypename, sizeof(s->f_fstypename), "unknown");
    s->f_mntonname[0] = '\0';
    s->f_mntfromname[0] = '\0';

    // macOS has no f_frsize, f_namelen, f_spare — ignore
}

/* -----------------------
 * FD-based statfs wrapper
 * ----------------------- */
static inline int linux_statfs_fd(int fd, struct linux_statfs *buf) {
    struct statfs s;

    if (fd < 0)
            return -EBADF;

    if (fstatfs(fd, &s) < 0)
            return -errno;

    statfs_to_linux(&s, buf);
    return 0;
}
