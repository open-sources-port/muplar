#pragma once

#ifdef __APPLE__
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys_compat/muplar_errno.h>
#include <stdio.h>
#include <stdlib.h>     // for arc4random or random()
#include <stdint.h>

/* ======================================================================= */

static inline int memfd_create(const char *name, unsigned int flags) {
    (void)flags; // ignore flags except CLOEXEC

    // Generate unique shm name
    char shm_name[64];
    // Use arc4random() for macOS randomness
    snprintf(shm_name, sizeof(shm_name), "/memfd_%d_%u", getpid(), (unsigned)arc4random());

    int fd = shm_open(shm_name, O_RDWR | O_CREAT | O_EXCL, 0600);
    if (fd < 0)
        return -1;

    // Unlink immediately to make it anonymous
    shm_unlink(shm_name);

    // CLOEXEC
    if (flags & 0x0001) // MFD_CLOEXEC
        fcntl(fd, F_SETFD, FD_CLOEXEC);

    return fd;
}

static inline int eventfd(unsigned int initval, int flags) {
    int pipefd[2];
    if (pipe(pipefd) < 0)
        return -1;

    // Non-blocking
    if (flags & 0x800) // EFD_NONBLOCK
        fcntl(pipefd[0], F_SETFL, O_NONBLOCK);
    if (flags & 0x800) // EFD_NONBLOCK
        fcntl(pipefd[1], F_SETFL, O_NONBLOCK);

    // CLOEXEC
    if (flags & 0x1) // EFD_CLOEXEC
        fcntl(pipefd[0], F_SETFD, FD_CLOEXEC);
    if (flags & 0x1) // EFD_CLOEXEC
        fcntl(pipefd[1], F_SETFD, FD_CLOEXEC);

    // Initialize counter
    if (initval > 0) {
        uint64_t val = initval;
        if (write(pipefd[1], &val, sizeof(val)) < 0) {
            close(pipefd[0]);
            close(pipefd[1]);
            return -1;
        }
    }

    // Return read end as the fd for simplicity; caller can use read/write
    return pipefd[0];
}

#endif
