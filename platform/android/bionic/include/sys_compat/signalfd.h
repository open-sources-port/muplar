// compat/signalfd.h
#pragma once

#ifdef __APPLE__

#include <signal.h>
#include <unistd.h>
#include <sys_compat/muplar_errno.h>
#include <stdlib.h>

/* Linux signalfd_siginfo layout (copied from <linux/signalfd.h>) */
struct signalfd_siginfo {
    uint32_t ssi_signo;    /* Signal number */
    int32_t  ssi_errno;    /* Error number */
    int32_t  ssi_code;     /* Signal code */
    uint32_t ssi_pid;      /* Sender's PID */
    uint32_t ssi_uid;      /* Sender's UID */
    int32_t  ssi_fd;       /* File descriptor (SIGIO) */
    uint32_t ssi_tid;      /* Kernel timer ID */
    uint32_t ssi_band;     /* Band event */
    uint32_t ssi_overrun;  /* Timer overrun count */
    uint32_t ssi_trapno;   /* Trap number that caused signal */
    int32_t  ssi_status;   /* Exit status or signal */
    int32_t  ssi_int;      /* Integer sent by sigqueue() */
    uint64_t ssi_ptr;      /* Pointer sent by sigqueue() */
    uint64_t ssi_utime;    /* User CPU time consumed */
    uint64_t ssi_stime;    /* System CPU time consumed */
    uint64_t ssi_addr;     /* Signal address */
    uint8_t  pad[48];      /* Reserved/padding */
};

typedef void (*signal_callback_t)(int signo, void *userdata);

struct signalfd_ctx {
    signal_callback_t callback;
    void *userdata;
    sigset_t mask;
};

/* static global pointer for callback context */
static struct signalfd_ctx *signalfd_ctx_global = NULL;

/* wrapper that matches sa_handler signature */
static void signalfd_global_wrapper(int signo) {
    if (signalfd_ctx_global && signalfd_ctx_global->callback) {
        signalfd_ctx_global->callback(signo, signalfd_ctx_global->userdata);
    }
}

static inline void signalfd_register_handler(
    signal_callback_t cb,
    void *userdata,
    const sigset_t *mask
) {
    struct signalfd_ctx *ctx = malloc(sizeof(*ctx));
    ctx->callback = cb;
    ctx->userdata = userdata;
    ctx->mask = *mask;

    /* store context globally (per translation unit) */
    signalfd_ctx_global = ctx;

    /* install handler */
    for (int signo = 1; signo < NSIG; signo++) {
        if (sigismember(mask, signo)) {
            struct sigaction sa;
            sa.sa_flags = 0;
            sa.sa_handler = signalfd_global_wrapper;  // correct
            sigemptyset(&sa.sa_mask);
            sigaction(signo, &sa, NULL);
        }
    }
}

#define SFD_NONBLOCK 0
#define SFD_CLOEXEC  0

static inline int signalfd(int fd, const sigset_t *mask, int flags) {
    (void)fd;
    (void)flags;
    signalfd_register_handler(NULL, NULL, mask); // or pass a proper callback if needed
    return -ENOSYS; // or return a fake FD if your code expects a positive value
}


#endif // __APPLE__
