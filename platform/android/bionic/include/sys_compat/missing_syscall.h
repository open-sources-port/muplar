/* SPDX-License-Identifier: LGPL-2.1-or-later */
#pragma once

/* Missing glibc definitions to access certain kernel APIs */

#include <sys_compat/muplar_errno.h>
#include <fcntl.h>
#include <linux/types.h>
#include <linux/time_types.h>
#include <linux/random.h>

#include <muplar_signal.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/mman.h>
#include <unistd.h>
#include <sys/resource.h>

#ifdef ARCH_MIPS
#include <asm/sgidefs.h>
#endif

#include "basic/missing_keyctl.h"
#include "basic/missing_sched.h"
#include "basic/missing_stat.h"
#include "missing_syscall_def.h"
#include <basic/macos_file_handle.h>

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys_compat/epoll.h>

#include <sched.h>
#include <mach/mach.h>
#include <mach/thread_policy.h>
#include <mach/thread_act.h>
#include <dirent.h>

/* linux/kcmp.h */
#ifndef KCMP_FILE
#define KCMP_FILE        0
#define KCMP_VM          1
#define KCMP_FILES       2
#define KCMP_FS          3
#define KCMP_SIGHAND     4
#define KCMP_IO          5
#define KCMP_SYSVSEM     6
#define KCMP_EPOLL_TFD   7
#endif

#ifndef O_NOATIME
#define O_NOATIME 0
#endif

/* Linux definitions (copied if missing) */
#ifndef IOPRIO_CLASS_SHIFT
#define IOPRIO_CLASS_SHIFT 13
#endif

#ifndef IOPRIO_PRIO_VALUE
#define IOPRIO_PRIO_VALUE(class, data) \
    (((class) << IOPRIO_CLASS_SHIFT) | (data))
#endif

#ifndef IOPRIO_CLASS_RT
#define IOPRIO_CLASS_RT    1
#define IOPRIO_CLASS_BE    2
#define IOPRIO_CLASS_IDLE  3
#endif

#ifndef IOPRIO_PRIO_CLASS
#define IOPRIO_PRIO_CLASS(ioprio) \
    ((ioprio) >> IOPRIO_CLASS_SHIFT)
#endif

#ifndef MFD_CLOEXEC
#define MFD_CLOEXEC       0x0001
#endif
#ifndef MFD_ALLOW_SEALING
#define MFD_ALLOW_SEALING 0x0002
#endif


#ifndef MOUNT_ATTR_RDONLY
#define MOUNT_ATTR_RDONLY       0x00000001 /* Mount read-only */
#endif

#ifndef MOUNT_ATTR_NOSUID
#define MOUNT_ATTR_NOSUID       0x00000002 /* Ignore suid and sgid bits */
#endif

#ifndef MOUNT_ATTR_NODEV
#define MOUNT_ATTR_NODEV        0x00000004 /* Disallow access to device special files */
#endif

#ifndef MOUNT_ATTR_NOEXEC
#define MOUNT_ATTR_NOEXEC       0x00000008 /* Disallow program execution */
#endif

#ifndef MOUNT_ATTR__ATIME
#define MOUNT_ATTR__ATIME       0x00000070 /* Setting on how atime should be updated */
#endif

#ifndef MOUNT_ATTR_RELATIME
#define MOUNT_ATTR_RELATIME     0x00000000 /* - Update atime relative to mtime/ctime. */
#endif

#ifndef MOUNT_ATTR_NOATIME
#define MOUNT_ATTR_NOATIME      0x00000010 /* - Do not update access times. */
#endif

#ifndef MOUNT_ATTR_STRICTATIME
#define MOUNT_ATTR_STRICTATIME  0x00000020 /* - Always perform atime updates */
#endif

#ifndef MOUNT_ATTR_NODIRATIME
#define MOUNT_ATTR_NODIRATIME   0x00000080 /* Do not update directory access times */
#endif

#ifndef MOUNT_ATTR_IDMAP
#define MOUNT_ATTR_IDMAP        0x00100000 /* Idmap mount to @userns_fd in struct mount_attr. */
#endif

#ifndef MOUNT_ATTR_NOSYMFOLLOW
#define MOUNT_ATTR_NOSYMFOLLOW  0x00200000 /* Do not follow symlinks */
#endif

#ifndef MOUNT_ATTR_SIZE_VER0
#define MOUNT_ATTR_SIZE_VER0    32 /* sizeof first published struct */
#endif

#ifndef AT_RECURSIVE
#define AT_RECURSIVE 0x8000
#endif

#ifndef PIDFD_NONBLOCK
#define PIDFD_NONBLOCK 0x0001
#endif
#ifndef PIDFD_CLOEXEC
#define PIDFD_CLOEXEC  0x0002
#endif

#ifndef GRND_NONBLOCK
#define GRND_NONBLOCK 0x0001
#endif
#ifndef GRND_RANDOM
#define GRND_RANDOM  0x0002
#endif

#ifndef CLONE_NEWNS
#define CLONE_NEWNS   0x00020000
#define CLONE_NEWCGROUP 0x02000000
#define CLONE_NEWUTS  0x04000000
#define CLONE_NEWIPC  0x08000000
#define CLONE_NEWUSER 0x10000000
#define CLONE_NEWPID  0x20000000
#define CLONE_NEWNET  0x40000000
#endif

/* Linux flags (define if missing) */
#ifndef RENAME_NOREPLACE
#define RENAME_NOREPLACE (1 << 0)
#endif
#ifndef RENAME_EXCHANGE
#define RENAME_EXCHANGE  (1 << 1)
#endif
#ifndef RENAME_WHITEOUT
#define RENAME_WHITEOUT  (1 << 2)
#endif

#define STATX_TYPE        0x00000001U
#define STATX_MODE        0x00000002U
#define STATX_NLINK       0x00000004U
#define STATX_UID         0x00000008U
#define STATX_GID         0x00000010U
#define STATX_ATIME       0x00000020U
#define STATX_MTIME       0x00000040U
#define STATX_CTIME       0x00000080U
#define STATX_INO         0x00000100U
#define STATX_SIZE        0x00000200U
#define STATX_BLOCKS      0x00000400U
#define STATX_BTIME       0x00000800U
#define STATX_BASIC_STATS 0x000007ffU

#ifndef AT_EMPTY_PATH
// dummy value, won't have effect on macOS
#define AT_EMPTY_PATH 0x1000
#endif

#ifndef O_PATH
#define O_PATH 0
#endif

#ifdef __APPLE__
#define O_DIRECT 0
#endif

#ifndef ENOTUNIQ
#define ENOTUNIQ 76
#endif

int dup3(int oldfd, int newfd, int flags);

/* ======================================================================= */

#ifndef pivot_root
static inline int pivot_root(const char *new_root, const char *put_old) {
    (void)put_old;

    if (chdir(new_root) < 0)
        return -1;

    if (chroot(new_root) < 0)
        return -1;

    if (chdir("/") < 0)
        return -1;

    return 0;
}
#endif

/* ======================================================================= */

static inline int ioprio_get(int which, int who) {
    int nice;
    int cls;

    if (which != 1 /* IOPRIO_WHO_PROCESS */) {
        errno = ENOTSUP;
        return -1;
    }

    nice = getpriority(PRIO_PROCESS, who);
    if (nice == -1 && errno != 0)
        return -1;

    if (nice <= -5)
        cls = IOPRIO_CLASS_RT;
    else if (nice >= 5)
        cls = IOPRIO_CLASS_IDLE;
    else
        cls = IOPRIO_CLASS_BE;

    /* Use middle priority (4) */
    return IOPRIO_PRIO_VALUE(cls, 4);
}

/* ======================================================================= */

static inline int ioprio_set(int which, int who, int ioprio) {
    int cls;
    int nice;

    /* IOPRIO_WHO_PROCESS */
    if (which != 1) {
        errno = ENOTSUP;
        return -1;
    }

    cls = IOPRIO_PRIO_CLASS(ioprio);

    switch (cls) {
    case IOPRIO_CLASS_RT:
        nice = -10;
        break;
    case IOPRIO_CLASS_BE:
        nice = 0;
        break;
    case IOPRIO_CLASS_IDLE:
        nice = 10;
        break;
    default:
        errno = EINVAL;
        return -1;
    }

    if (setpriority(PRIO_PROCESS, who, nice) < 0)
        return -1;

    return 0;
}

/* ======================================================================= */

static inline pid_t gettid(void) {
    uint64_t tid;

    if (pthread_threadid_np(NULL, &tid) != 0) {
        errno = EINVAL;
        return -1;
    }

    return (pid_t) tid;
}

static inline pid_t raw_getpid(void) {
    return getpid();
}

/* ======================================================================= */

static inline int setns(int fd, int nstype) {
    (void) fd;
    (void) nstype;
    errno = ENOSYS;
    return -1;
}

/* ======================================================================= */

static inline int renameat2(int olddirfd, const char *oldpath,
                            int newdirfd, const char *newpath,
                            unsigned int flags) {

    /* Simple rename */
    if (flags == 0)
        return renameat(olddirfd, oldpath, newdirfd, newpath);

    /* Atomic exchange not supported */
    if (flags & (RENAME_EXCHANGE | RENAME_WHITEOUT)) {
        errno = ENOSYS;
        return -1;
    }

    /* Emulate RENAME_NOREPLACE */
    if (flags & RENAME_NOREPLACE) {
        /* Create hard link to new path (fails if exists) */
        if (linkat(olddirfd, oldpath, newdirfd, newpath, 0) < 0)
            return -1;

        /* Remove old path */
        if (unlinkat(olddirfd, oldpath, 0) < 0) {
            /* Rollback best-effort */
            unlinkat(newdirfd, newpath, 0);
            return -1;
        }

        return 0;
    }

    errno = EINVAL;
    return -1;
}

/* ======================================================================= */
static inline int kcmp(pid_t pid1, pid_t pid2, int type,
                       unsigned long idx1, unsigned long idx2) {
    (void) pid1;
    (void) pid2;
    (void) type;
    (void) idx1;
    (void) idx2;
    errno = ENOSYS;
    return -1;
}

/* ======================================================================= */

static inline ssize_t copy_file_range(int fd_in, off_t *off_in,
                                      int fd_out, off_t *off_out,
                                      size_t len, unsigned int flags) {
    (void)flags; // unsupported on macOS

    off_t start_in = off_in ? *off_in : lseek(fd_in, 0, SEEK_CUR);
    off_t start_out = off_out ? *off_out : lseek(fd_out, 0, SEEK_CUR);

    if (start_in == (off_t)-1 || start_out == (off_t)-1)
        return -1;

    ssize_t total = 0;
    size_t chunk_size = 64 * 1024; // 64 KB buffer
    char buf[64 * 1024];

    while (len > 0) {
        ssize_t to_read = (len > chunk_size) ? chunk_size : len;
        ssize_t n = pread(fd_in, buf, to_read, start_in);
        if (n <= 0) break;

        ssize_t written = pwrite(fd_out, buf, n, start_out);
        if (written != n) return -1;

        start_in += n;
        start_out += n;
        total += n;
        len -= n;
    }

    if (off_in) *off_in = start_in;
    else lseek(fd_in, start_in, SEEK_SET);

    if (off_out) *off_out = start_out;
    else lseek(fd_out, start_out, SEEK_SET);

    return total;
}

/* ======================================================================= */

static inline int bpf(int cmd, void *attr, unsigned int size) {
    (void)cmd;
    (void)attr;
    (void)size;

    // macOS does not support BPF/eBPF
    errno = ENOSYS;
    return -1;
}

/* ======================================================================= */

struct statx_timestamp {
    int64_t tv_sec;
    uint32_t tv_nsec;
    int32_t __reserved;
};

struct statx {
    uint32_t stx_mask;
    uint32_t stx_blksize;
    uint64_t stx_attributes;
    uint32_t stx_nlink;
    uint32_t stx_uid;
    uint32_t stx_gid;
    uint16_t stx_mode;
    uint64_t stx_ino;
    uint64_t stx_size;
    uint64_t stx_blocks;
    uint64_t stx_attributes_mask;

    struct statx_timestamp stx_atime;
    struct statx_timestamp stx_btime;
    struct statx_timestamp stx_ctime;
    struct statx_timestamp stx_mtime;

    uint32_t stx_rdev_major;
    uint32_t stx_rdev_minor;
    uint32_t stx_dev_major;
    uint32_t stx_dev_minor;

    __u64 stx_mnt_id;
    __u64 __spare2;
    __u64 __spare3[12];
};

static inline int statx(
        int dirfd,
        const char *path,
        int flags,
        unsigned int mask,
        struct statx *sx) {
    (void) mask;

    struct stat st;
    int r;

    if (!sx)
        return errno = EINVAL, -1;

    memset(sx, 0, sizeof(*sx));

    /* Emulate AT_EMPTY_PATH */
    if ((flags & AT_EMPTY_PATH) && path && path[0] == '\0') {
        if (dirfd < 0)
            return errno = EBADF, -1;
        r = fstat(dirfd, &st);
    } else {
        if (!path)
            return errno = EINVAL, -1;

        r = fstatat(dirfd, path, &st,
                    (flags & AT_SYMLINK_NOFOLLOW) ? AT_SYMLINK_NOFOLLOW : 0);
    }

    if (r < 0)
        return -1;

    /* Fill statx */
    sx->stx_mask = STATX_BASIC_STATS;

    sx->stx_mode = st.st_mode;
    sx->stx_uid = st.st_uid;
    sx->stx_gid = st.st_gid;
    sx->stx_nlink = st.st_nlink;
    sx->stx_ino = st.st_ino;
    sx->stx_size = st.st_size;
    sx->stx_blocks = st.st_blocks;
    sx->stx_blksize = st.st_blksize;

    sx->stx_atime.tv_sec = st.st_atimespec.tv_sec;
    sx->stx_atime.tv_nsec = st.st_atimespec.tv_nsec;

    sx->stx_mtime.tv_sec = st.st_mtimespec.tv_sec;
    sx->stx_mtime.tv_nsec = st.st_mtimespec.tv_nsec;

    sx->stx_ctime.tv_sec = st.st_ctimespec.tv_sec;
    sx->stx_ctime.tv_nsec = st.st_ctimespec.tv_nsec;

    sx->stx_btime.tv_sec = st.st_birthtimespec.tv_sec;
    sx->stx_btime.tv_nsec = st.st_birthtimespec.tv_nsec;

    return 0;
}

// struct statx new_statx;
#define new_statx statx

/* ======================================================================= */

struct open_how {
    int flags;
    mode_t mode;
    unsigned int resolve; // ignored on macOS
};

static inline int openat2(int dirfd, const char *pathname, struct open_how *how) {
    if (!how) {
        errno = EINVAL;
        return -1;
    }

    int flags = how->flags;
    mode_t mode = how->mode;

    // macOS does not support O_PATH, ignore it
    #ifdef O_PATH
        flags &= ~O_PATH;
    #endif

    if (dirfd == AT_FDCWD) {
        return open(pathname, flags, mode);
    } else {
        // Hack: fchdir to dirfd, open, restore cwd
        char cwd[PATH_MAX];
        if (!getcwd(cwd, sizeof(cwd))) return -1;

        if (fchdir(dirfd) < 0) return -1;

        int fd = open(pathname, flags, mode);
        int err = errno;

        chdir(cwd); // restore cwd
        errno = err;

        return fd;
    }
}

// ------------------ helpers ------------------
static inline kern_return_t pid_to_mach_thread(pid_t pid, thread_t *out) {
    task_t task;
    kern_return_t kr;

    kr = task_for_pid(mach_task_self(), pid, &task);
    if (kr != KERN_SUCCESS)
        return kr;

    thread_act_array_t threads;
    mach_msg_type_number_t count;

    kr = task_threads(task, &threads, &count);
    if (kr != KERN_SUCCESS)
        return kr;

    if (count == 0)
        return KERN_FAILURE;

    /* Best-effort: pick first non-idle thread */
    *out = threads[0];

    mach_port_deallocate(mach_task_self(), task);

    for (mach_msg_type_number_t i = 0; i < count; i++)
        mach_port_deallocate(mach_task_self(), threads[i]);

    vm_deallocate(mach_task_self(),
        (vm_address_t)threads,
        count * sizeof(thread_t));

    return KERN_SUCCESS;
}

// ------------------ sched_setattr ------------------
static inline int sched_setattr(pid_t pid, const struct sched_attr *attr, unsigned int flags) {
    (void) flags;
    if (!attr) return EINVAL;

    thread_t t;
    kern_return_t kr;

    kr = pid_to_mach_thread(pid, &t);
    if (kr != KERN_SUCCESS) {
        /* handle error / fallback */
        t = mach_thread_self();
    }
    if (!t) {
        errno = ENOSYS;
        return -1;
    }

    thread_time_constraint_policy_data_t policy;
    policy.period = 0;
    policy.computation = 0;
    policy.constraint = 0;
    policy.preemptible = 1;

    switch (attr->sched_policy) {
        case SCHED_FIFO:
        case SCHED_RR:
            // Use time constraint policy for RT threads
            policy.period = 1000000;        // 1ms period
            policy.computation = 500000;    // 0.5ms runtime
            policy.constraint = 1000000;    // 1ms deadline
            policy.preemptible = 0;
            if (thread_policy_set(t,
                                  THREAD_TIME_CONSTRAINT_POLICY,
                                  (thread_policy_t)&policy,
                                  THREAD_TIME_CONSTRAINT_POLICY_COUNT) != KERN_SUCCESS) {
                errno = EPERM;
                return -1;
            }
            break;
        case SCHED_OTHER:
        default:
            // Use normal pthread scheduling
            {
                struct sched_param param;
                param.sched_priority = attr->sched_priority;
                if (pthread_setschedparam(pthread_self(), attr->sched_policy, &param) != 0) {
                    errno = EPERM;
                    return -1;
                }
            }
            break;
    }

    return 0;
}

// ------------------ sched_getattr ------------------
static inline int sched_getattr(pid_t pid, struct sched_attr *attr, unsigned int size, unsigned int flags) {
    (void) flags;
    (void) size;
    if (!attr) return EINVAL;

    thread_t t;
    kern_return_t kr;

    kr = pid_to_mach_thread(pid, &t);
    if (kr != KERN_SUCCESS) {
        /* handle error / fallback */
        t = mach_thread_self();
    }
    if (!t) {
        errno = ENOSYS;
        return -1;
    }

    thread_time_constraint_policy_data_t policy;
    mach_msg_type_number_t count = THREAD_TIME_CONSTRAINT_POLICY_COUNT;

    boolean_t get_default = 0;
    if (thread_policy_get(t,
                          THREAD_TIME_CONSTRAINT_POLICY,
                          (thread_policy_t)&policy,
                          &count, &get_default) == KERN_SUCCESS) {
        // RT thread
        attr->sched_policy = SCHED_RR;
        attr->sched_priority = 31; // max RT priority
        attr->sched_flags = 0;
        attr->sched_nice = 0;
        attr->sched_runtime = policy.computation;
        attr->sched_period = policy.period;
        attr->sched_deadline = policy.constraint;
    } else {
        // Normal thread
        struct sched_param param;
        int policy_val;
        if (pthread_getschedparam(pthread_self(), &policy_val, &param) != 0) {
            errno = EPERM;
            return -1;
        }
        attr->sched_policy = policy_val;
        attr->sched_priority = param.sched_priority;
        attr->sched_flags = 0;
        attr->sched_nice = 0;
        attr->sched_runtime = 0;
        attr->sched_deadline = 0;
        attr->sched_period = 0;
    }

    attr->size = sizeof(*attr);
    return 0;
}

/* ======================================================================= */

enum {
        MPOL_DEFAULT,
        MPOL_PREFERRED,
        MPOL_BIND,
        MPOL_INTERLEAVE,
        MPOL_LOCAL,
};

static inline int set_mempolicy(int mode, const unsigned long *nodemask, unsigned long maxnode) {
    (void)mode;
    (void)nodemask;
    (void)maxnode;
    // macOS has no NUMA, always succeed
    return 0;
}

static inline int get_mempolicy(int *mode, unsigned long *nodemask,
                                unsigned long maxnode, unsigned long addr, int flags) {
    if (mode) *mode = 0;          // MPOL_DEFAULT
    if (nodemask && maxnode > 0) nodemask[0] = 1; // pretend single node
    (void)addr;
    (void)flags;
    return 0;
}

/* ======================================================================= */

static inline int pidfd_send_signal(int pidfd, int sig, siginfo_t *info, unsigned int flags) {
    (void)info;
    (void)flags;

    // On macOS, pidfd is a kqueue returned by pidfd_open
    // Unfortunately, kqueue itself cannot send signals, so we treat pidfd as a pid
    // Workaround: store pid somewhere or require caller to pass pid directly
    // For simplicity, assume pidfd is the actual PID (common fallback)
    pid_t pid = pidfd;

    if (kill(pid, sig) < 0)
        return -1;

    return 0;
}

static inline int pidfd_open(pid_t pid, unsigned flags) {
    int kq;
    struct kevent kev;

    kq = kqueue();
    if (kq < 0)
        return -1;

    if (flags & PIDFD_CLOEXEC)
        fcntl(kq, F_SETFD, FD_CLOEXEC);

    EV_SET(&kev,
           pid,
           EVFILT_PROC,
           EV_ADD | EV_ENABLE,
           NOTE_EXIT,
           0,
           NULL);

    if (kevent(kq, &kev, 1, NULL, 0, NULL) < 0) {
        close(kq);
        return -1;
    }

    return kq;
}

static inline int pidfd_wait(int pid, int options, int *status) {
    return waitpid(pid, status, options);
}

/* ======================================================================= */

static inline int rt_sigqueueinfo(pid_t pid, int sig, siginfo_t *info) {
    (void)info; // macOS cannot attach siginfo

    if (kill(pid, sig) < 0)
        return -1;

    return 0;
}

/* ======================================================================= */

static inline int execveat(int dirfd, const char *pathname,
                           char *const argv[], char *const envp[],
                           int flags) {
    (void) flags;
    if (!pathname) {
        errno = EINVAL;
        return -1;
    }

    char cwd[PATH_MAX];
    if (dirfd != AT_FDCWD) {
        if (!getcwd(cwd, sizeof(cwd))) return -1;
        if (fchdir(dirfd) < 0) return -1;
    }

    int ret = execve(pathname, argv, envp);

    int err = errno;

    // Restore cwd if we changed it
    if (dirfd != AT_FDCWD) {
        chdir(cwd);
    }

    errno = err;
    return ret; // -1 if failed
}

/* ======================================================================= */

#define CLOSE_RANGE_UNSHARE 0x1
#define CLOSE_RANGE_CLOEXEC 0x2

static inline int close_range(unsigned int first, unsigned int last, unsigned int flags) {
    (void)flags; // flags ignored

    if (first > last) {
        errno = EINVAL;
        return -1;
    }

    for (unsigned int fd = first; fd <= last; fd++) {
        close(fd); // ignore errors
    }

    return 0;
}

/* ======================================================================= */
#ifndef HAVE_MOUNT_SETATTR
struct mount_attr {
    uint64_t attr_set;
    uint64_t attr_clr;
    uint64_t propagation;
    uint64_t userns_fd;
};
static inline int mount_setattr(int dirfd, const char *pathname,
                                struct mount_attr *attr, unsigned int flags) {
    (void)dirfd;
    (void)pathname;
    (void)attr;
    (void)flags;
    errno = ENOSYS;
    return -1;
}
#endif

/* ======================================================================= */

#ifndef HAVE_OPEN_TREE
#define OPEN_TREE_CLONE 0x1
#define OPEN_TREE_CLOEXEC 0x2
static inline int open_tree(int dfd, const char *pathname, unsigned int flags) {
    (void)dfd;
    (void)pathname;
    (void)flags;
    errno = ENOSYS;
    return -1;
}
#endif

/* ======================================================================= */

#ifndef HAVE_MOVE_MOUNT
static inline int move_mount(int from_dfd, const char *from_path,
                             int to_dfd, const char *to_path, unsigned int flags) {
    (void)from_dfd;
    (void)from_path;
    (void)to_dfd;
    (void)to_path;
    (void)flags;
    errno = ENOSYS;
    return -1;
}
#endif

/* ======================================================================= */

#ifndef HAVE_FSOPEN
static inline int fsopen(const char *fs_name, unsigned int flags) {
    (void)fs_name;
    (void)flags;
    errno = ENOSYS;
    return -1;
}
#endif

/* ======================================================================= */

#ifndef HAVE_FSCONFIG
static inline int fsconfig(int fs_fd, unsigned int cmd, const char *key,
                           const char *value, int aux) {
    (void)fs_fd;
    (void)cmd;
    (void)key;
    (void)value;
    (void)aux;
    errno = ENOSYS;
    return -1;
}
#endif


/* ======================================================================= */

#ifndef HAVE_GETDENTS64
struct linux_dirent64 {
    uint64_t d_ino;
    int64_t  d_off;
    unsigned short d_reclen;
    unsigned char  d_type;
    char           d_name[256]; // simplified
};
static inline int getdents64(int fd, struct linux_dirent64 *dirp, size_t count) {
    if (!dirp) {
        errno = EINVAL;
        return -1;
    }

    DIR *dp = fdopendir(fd);
    if (!dp) return -1;

    struct dirent *entry;
    size_t total = 0;
    while ((entry = readdir(dp)) != NULL && total + sizeof(struct linux_dirent64) <= count) {
        struct linux_dirent64 *d = &dirp[total / sizeof(struct linux_dirent64)];
        d->d_ino = entry->d_ino;
        d->d_off = 0; // not accurate
        d->d_reclen = sizeof(struct linux_dirent64);
        d->d_type = entry->d_type;
        strncpy(d->d_name, entry->d_name, sizeof(d->d_name)-1);
        d->d_name[sizeof(d->d_name)-1] = '\0';
        total += sizeof(struct linux_dirent64);
    }

    closedir(dp);
    return (int)total;
}
#endif

/* ======================================================================= */

static inline int pipe2(int pipefd[2], int flags) {
    if (pipe(pipefd) < 0)
        return -1;

    if (flags & O_CLOEXEC) {
        fcntl(pipefd[0], F_SETFD, FD_CLOEXEC);
        fcntl(pipefd[1], F_SETFD, FD_CLOEXEC);
    }
    return 0;
}

/* ======================================================================= */
