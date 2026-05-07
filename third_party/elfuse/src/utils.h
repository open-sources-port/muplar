/* Shared utility helpers
 *
 * Copyright 2026 elfuse contributors
 * SPDX-License-Identifier: Apache-2.0
 *
 * Small inline helpers and macros used across multiple modules. Keeps leaf
 * utilities in one place so they do not accumulate as single-function headers.
 */

#pragma once

#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

/* Align x up to the next multiple of a; a must be a power of two.
 * Both x and a are evaluated as uint64_t. Most callers manipulate guest
 * addresses or sizes that already fit, so this avoids surprises from
 * signed/unsigned mixing in alignment masks.
 */
#define ALIGN_UP(x, a) \
    (((uint64_t) (x) + ((uint64_t) (a) - 1)) & ~((uint64_t) (a) - 1))

/* Align x down to the previous multiple of a; a must be a power of two. */
#define ALIGN_DOWN(x, a) ((uint64_t) (x) & ~((uint64_t) (a) - 1))

/* The Linux ABI fixes the page size at 4KiB on aarch64 regardless of the host
 * page size, so this is shared by every guest memory path (mmap, brk,
 * mprotect, ELF loading).
 */
#define GUEST_PAGE_SIZE 4096ULL
#define PAGE_ALIGN_UP(x) ALIGN_UP(x, GUEST_PAGE_SIZE)

/* 2MiB block alignment shared by region setup, page table walking, and stack
 * placement. BLOCK_2MIB itself is defined in core/guest.h.
 */
#define ALIGN_2MIB_DOWN(x) ALIGN_DOWN(x, 2ULL * 1024 * 1024)
#define ALIGN_2MIB_UP(x) ALIGN_UP(x, 2ULL * 1024 * 1024)

/* Branchless range check: true when minx <= x < minx + size.
 *
 * Replaces the recurring pair `x >= minx && x < minx + size` with a single
 * unsigned compare: shift x into a [0, size) window and let unsigned
 * wraparound flag both underflow (x < minx) and overflow (x >= minx + size).
 * Width-safe for any operand up to uint64_t.
 *
 * Operands are cast to uint64_t *before* the subtraction so signed inputs
 * near the type extremes (e.g., LONG_MIN passed by a strtol result) cannot
 * trigger signed overflow UB. Negative signed values sign-extend through the
 * unsigned conversion to a large uint64_t, which still yields the correct
 * out-of-range answer.
 *
 * Caveat: x and minx are evaluated twice; do not pass expressions with side
 * effects.
 */
#define RANGE_CHECK(x, minx, size) \
    (((uint64_t) (x) - (uint64_t) (minx)) < (uint64_t) (size))

/* Number of elements in a fixed-size array. */
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

#ifndef MIN
#define MIN(a, b)               \
    ({                          \
        __typeof__(a) _a = (a); \
        __typeof__(b) _b = (b); \
        _a < _b ? _a : _b;      \
    })
#endif

#ifndef MAX
#define MAX(a, b)               \
    ({                          \
        __typeof__(a) _a = (a); \
        __typeof__(b) _b = (b); \
        _a > _b ? _a : _b;      \
    })
#endif

/* Copy src into dst, truncating to dst_size-1 and always NUL-terminating.
 * Returns strlen(src) so the caller can detect truncation (ret >= dst_size).
 */
static inline size_t str_copy_trunc(char *dst, const char *src, size_t dst_size)
{
    size_t src_len = strlen(src);

    if (dst_size > 0) {
        size_t copy_len = src_len < dst_size ? src_len : dst_size - 1;
        memcpy(dst, src, copy_len);
        dst[copy_len] = '\0';
    }

    return src_len;
}

/* close(2) on a cleanup path: preserves errno across the close so the
 * caller's failure errno survives untouched. Skips the close when fd < 0.
 */
static inline void close_keep_errno(int fd)
{
    int saved = errno;
    if (fd >= 0)
        (void) close(fd);
    errno = saved;
}

/* Enable an fd flag if it is not already set. Returns 0 on success or -1 with
 * errno preserved from the failing fcntl call.
 */
static inline int fd_set_fd_flag(int fd, int flag)
{
    int flags = fcntl(fd, F_GETFD);

    if (flags < 0)
        return -1;
    if ((flags & flag) != 0)
        return 0;
    return fcntl(fd, F_SETFD, flags | flag);
}

/* Set or clear a file status flag such as O_NONBLOCK. */
static inline int fd_update_status_flag(int fd, int flag, bool enabled)
{
    int flags = fcntl(fd, F_GETFL);

    if (flags < 0)
        return -1;
    if (enabled)
        flags |= flag;
    else
        flags &= ~flag;
    return fcntl(fd, F_SETFL, flags);
}

static inline int fd_set_cloexec(int fd)
{
    return fd_set_fd_flag(fd, FD_CLOEXEC);
}

static inline int fd_set_nonblock(int fd)
{
    return fd_update_status_flag(fd, O_NONBLOCK, true);
}

/* Carry overflow/underflow between tv_nsec and tv_sec so the result is a
 * canonical timespec with 0 <= tv_nsec < 1e9. Uses div/mod (which truncate
 * toward zero in C99) plus a single borrow so the LONG_MIN case never
 * negates tv_nsec — that would be undefined behavior.
 *
 * NSEC_PER_SEC is also defined by mach/clock_types.h and dispatch/time.h
 * on macOS; the guard avoids redefinition warnings when those system
 * headers are pulled in transitively.
 */
#ifndef NSEC_PER_SEC
#define NSEC_PER_SEC 1000000000L
#endif

static inline void timespec_normalize(struct timespec *ts)
{
    ts->tv_sec += ts->tv_nsec / (long) NSEC_PER_SEC;
    ts->tv_nsec %= (long) NSEC_PER_SEC;
    if (ts->tv_nsec < 0) {
        ts->tv_sec -= 1;
        ts->tv_nsec += (long) NSEC_PER_SEC;
    }
}

/* Compute a CLOCK_REALTIME absolute deadline that is rel_ms milliseconds in
 * the future, suitable for pthread_cond_timedwait().
 */
static inline void timespec_deadline_in_ms(struct timespec *out, long rel_ms)
{
    clock_gettime(CLOCK_REALTIME, out);
    out->tv_sec += rel_ms / 1000;
    out->tv_nsec += (rel_ms % 1000) * 1000000L;
    timespec_normalize(out);
}

/* Bitmap helpers.
 *
 * Operate on a single uint64_t word. For multi-word bitmaps, callers index
 * the word and pass the bit position within it. Centralizing the shift and
 * compiler-intrinsic calls here keeps the meaning ("the bit for slot N",
 * "lowest set bit") visible at the call site instead of leaving readers to
 * decode `1ULL << (n)` and `__builtin_ctzll`.
 */

/* The bit value for position n (0..63). n is evaluated once. */
#define BIT64(n) (1ULL << (n))

/* Mask of the low n bits. n may be 0..64. */
static inline uint64_t bit_mask64_low(unsigned int n)
{
    return n >= 64 ? UINT64_MAX : (BIT64(n) - 1);
}

/* Position of the lowest set bit. word must be non-zero — __builtin_ctzll
 * is undefined on zero. Range: 0..63.
 */
static inline int bit_ctz64(uint64_t word)
{
    return __builtin_ctzll(word);
}

/* Number of set bits in word. */
static inline int bit_popcount64(uint64_t word)
{
    return __builtin_popcountll(word);
}

/* Compiler attribute wrappers.
 *
 * PACKED removes inter-field padding, used for Linux ABI structures whose
 * layout must match the kernel exactly (e.g., linux_dirent64). Apply at the
 * end of a struct definition: `} PACKED name_t;`.
 */
#define PACKED __attribute__((packed))
