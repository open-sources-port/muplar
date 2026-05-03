#pragma once

#include <sys_compat/muplar_errno.h>
#include <pthread.h>
#include <unistd.h>
#include <linux/capability.h>
#include <linux/types.h>

#ifdef __cplusplus
extern "C" {
#endif

/* --- Linux prctl constants --- */
#define PR_SET_NAME            15
#define PR_GET_NAME            16
#define PR_SET_PDEATHSIG       1
#define PR_GET_PDEATHSIG       2
#define PR_SET_DUMPABLE        4
#define PR_GET_DUMPABLE        3
#define PR_SET_NO_NEW_PRIVS    38
#define PR_CAPBSET_READ  1001
#define PR_CAPBSET_DROP  1002

#define TASK_COMM_LEN 16

/* --- prctl stub for macOS --- */
static inline int prctl(int option,
                        unsigned long arg2,
                        unsigned long arg3,
                        unsigned long arg4,
                        unsigned long arg5)
{
    (void) arg3;
    (void) arg4;
    (void) arg5;

    switch (option) {
    case PR_SET_NAME: {
        const char *name = (const char *) arg2;
        if (!name) return -EINVAL;
        int r = pthread_setname_np(name);
        return r == 0 ? 0 : -r;
    }
    case PR_GET_NAME: {
        char *buf = (char *) arg2;
        if (!buf) return -EINVAL;
        int r = pthread_getname_np(pthread_self(), buf, TASK_COMM_LEN);
        return r == 0 ? 0 : -r;
    }
    case PR_SET_DUMPABLE:
    case PR_GET_DUMPABLE:
        return 0;
    case PR_SET_PDEATHSIG:
    case PR_GET_PDEATHSIG:
    case PR_SET_NO_NEW_PRIVS:
        return -ENOSYS;
    case PR_CAPBSET_READ:
    case PR_CAPBSET_DROP:
        return 0;
    default:
        return -EINVAL;
    }
}

/* --- Linux capability stubs for macOS --- */
typedef void* cap_t;

#define CAP_EFFECTIVE   0
#define CAP_INHERITABLE 0
#define CAP_PERMITTED   0
#define CAP_CLEAR       0

static inline cap_t cap_dup(cap_t caps) { (void)caps; return NULL; }
static inline int cap_compare(cap_t a, cap_t b) { (void)a; (void)b; return 0; }

/* --- macOS replacement for setresgid --- */
static inline int setresgid(gid_t rgid, gid_t egid, gid_t sgid) {
    if (setregid(rgid, egid) < 0) return -1;
    if (setegid(sgid) < 0) return -1;
    return 0;
}

#ifdef __cplusplus
}
#endif
