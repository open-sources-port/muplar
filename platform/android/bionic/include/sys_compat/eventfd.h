#pragma once
#include <unistd.h>
#include <stdint.h>
#include <stdlib.h>
#include <fcntl.h>

#ifdef __APPLE__

#define EFD_CLOEXEC 1
#define EFD_NONBLOCK 2

typedef struct {
    int read_fd;
    int write_fd;
} eventfd_t;

static inline intptr_t eventfd(unsigned int initval, int flags) {
    eventfd_t *e = malloc(sizeof(eventfd_t));
    if (!e) return -1;

    int fds[2];
    if (pipe(fds) != 0) {
        free(e);
        return -1;
    }

    if (flags & EFD_NONBLOCK) {
        fcntl(fds[0], F_SETFL, O_NONBLOCK);
        fcntl(fds[1], F_SETFL, O_NONBLOCK);
    }

    if (flags & EFD_CLOEXEC) {
        fcntl(fds[0], F_SETFD, FD_CLOEXEC);
        fcntl(fds[1], F_SETFD, FD_CLOEXEC);
    }

    e->read_fd = fds[0];
    e->write_fd = fds[1];

    return (intptr_t)e;
}

static inline int eventfd_write(intptr_t efd, uint64_t value) {
    eventfd_t *e = (eventfd_t *)efd;
    ssize_t r = write(e->write_fd, &value, sizeof(value));
    return (r == sizeof(value)) ? 0 : -1;
}

static inline int eventfd_read(intptr_t efd, uint64_t *value) {
    eventfd_t *e = (eventfd_t *)efd;
    ssize_t r = read(e->read_fd, value, sizeof(*value));
    return (r == sizeof(*value)) ? 0 : -1;
}

static inline int eventfd_close(intptr_t efd) {
    eventfd_t *e = (eventfd_t *)efd;
    int r1 = close(e->read_fd);
    int r2 = close(e->write_fd);
    free(e);
    return (r1 == 0 && r2 == 0) ? 0 : -1;
}

#endif
