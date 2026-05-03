#pragma once

#ifdef __APPLE__

#include <fcntl.h>
#include <unistd.h>
#include <sys_compat/muplar_errno.h>
#include <string.h>

// Linux constants
#define FALLOC_FL_KEEP_SIZE   0x01
#define FALLOC_FL_PUNCH_HOLE  0x02
#define FALLOC_FL_ZERO_RANGE  0x10

// macOS-compatible fallocate
static inline int fallocate(int fd, int mode, off_t offset, off_t len) {
    if (len <= 0) return 0; // nothing to do

    // PUNCH_HOLE: approximate by zeroing the range
    if (mode & FALLOC_FL_PUNCH_HOLE) {
        if (lseek(fd, offset, SEEK_SET) == -1) return -1;
        char zero[4096];
        memset(zero, 0, sizeof(zero));
        off_t written = 0;
        while (written < len) {
            ssize_t to_write = (len - written) < (off_t)sizeof(zero) ? (len - written) : (off_t)sizeof(zero);
            ssize_t r = write(fd, zero, to_write);
            if (r <= 0) return -1;
            written += r;
        }
        return 0;
    }

    // ZERO_RANGE: zero the range, optionally extend file
    if (mode & FALLOC_FL_ZERO_RANGE) {
        if (lseek(fd, offset, SEEK_SET) == -1) return -1;
        char zero[4096];
        memset(zero, 0, sizeof(zero));
        off_t written = 0;
        while (written < len) {
            ssize_t to_write = (len - written) < (off_t)sizeof(zero) ? (len - written) : (off_t)sizeof(zero);
            ssize_t r = write(fd, zero, to_write);
            if (r <= 0) return -1;
            written += r;
        }

        if (!(mode & FALLOC_FL_KEEP_SIZE)) {
            if (ftruncate(fd, offset + len) < 0) return -1;
        }
        return 0;
    }

    // Default: allocate space using F_PREALLOCATE
    fstore_t fstore = {0};
    fstore.fst_flags = F_ALLOCATEALL;
    fstore.fst_posmode = F_PEOFPOSMODE;
    fstore.fst_offset = offset;
    fstore.fst_length = len;

    if (fcntl(fd, F_PREALLOCATE, &fstore) == -1)
        return -1;

    if (!(mode & FALLOC_FL_KEEP_SIZE)) {
        if (ftruncate(fd, offset + len) < 0)
            return -1;
    }

    return 0;
}

// macOS fallback for posix_fallocate()
static inline int posix_fallocate(int fd, off_t offset, off_t len) {
    int r = fallocate(fd, 0, offset, len);
    if (r == 0) return 0;
    return errno; // posix_fallocate returns positive errno
}

#endif // __APPLE__
