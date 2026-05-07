/* FD table: bitmap allocator, alloc/close/snapshot helpers
 *
 * Copyright 2026 elfuse contributors
 * Copyright 2025 Moritz Angermann, zw3rk pte. ltd.
 * SPDX-License-Identifier: Apache-2.0
 *
 * File descriptor table management for the guest. Uses a bitmap allocator for
 * O(1) lowest-free-FD lookup, with alloc/close/snapshot helpers that serialize
 * access through fd_lock.
 */

#include <stdatomic.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <dirent.h>
#include <pthread.h>

#include "utils.h"

#include "syscall/abi.h"
#include "syscall/internal.h"

/* Protects the FD table (fd_alloc, fd_alloc_at, fd_alloc_from, sys_close).
 * File descriptor operations from concurrent threads must be serialized.
 */
pthread_mutex_t fd_lock = PTHREAD_MUTEX_INITIALIZER; /* Lock order: 3 */

/* FD table. */
fd_entry_t fd_table[FD_TABLE_SIZE];

/* RLIMIT_NOFILE tracking. */
/* Guest-side soft limit for RLIMIT_NOFILE. fd_alloc checks this.
 * Default matches typical Linux default (1024). Updated by prlimit64.
 */
static _Atomic int rlimit_nofile_cur = FD_TABLE_SIZE;

void fd_set_rlimit_nofile(int cur)
{
    if (cur > FD_TABLE_SIZE)
        cur = FD_TABLE_SIZE;
    if (cur < 0)
        cur = 0;
    rlimit_nofile_cur = cur;
}

int fd_get_rlimit_nofile(void)
{
    return rlimit_nofile_cur;
}

/* Bitmap for O(1) lowest-free-FD allocation. A set bit means the FD is free
 * (FD_CLOSED). bit_ctz64 on each word finds the lowest free FD in O(1) per
 * word, vs O(FD_TABLE_SIZE) linear scan.
 */
#define FD_BITMAP_WORDS (FD_TABLE_SIZE / 64)
static uint64_t fd_free_bitmap[FD_BITMAP_WORDS];

static inline void fd_bitmap_set_free(int fd)
{
    fd_free_bitmap[fd / 64] |= BIT64(fd % 64);
}

static inline void fd_bitmap_set_used(int fd)
{
    fd_free_bitmap[fd / 64] &= ~BIT64(fd % 64);
}

static inline void fd_init_entry(int fd,
                                 int type,
                                 int host_fd,
                                 void (*cleanup)(int))
{
    fd_bitmap_set_used(fd);
    fd_table[fd].type = type;
    fd_table[fd].host_fd = host_fd;
    fd_table[fd].linux_flags = 0;
    fd_table[fd].dir = NULL;
    fd_table[fd].proc_path[0] = '\0';
    fd_table[fd].seals = 0;
    sock_opt_clear(&fd_table[fd]);
    fd_table[fd].cleanup = cleanup;
}

/* Find the lowest free FD >= minfd using the bitmap.
 * Returns -1 if no free FD exists at or above minfd.
 * Caller must hold fd_lock.
 */
static int fd_bitmap_find_free(int minfd)
{
    if (minfd < 0)
        minfd = 0;
    if (minfd >= FD_TABLE_SIZE)
        return -1;
    int word = minfd / 64, bit = minfd % 64;

    /* Check the partial first word (mask out bits below minfd) */
    uint64_t masked = fd_free_bitmap[word] & (~0ULL << bit);
    if (masked) {
        int fd = word * 64 + bit_ctz64(masked);
        return (fd < FD_TABLE_SIZE) ? fd : -1;
    }

    /* Check remaining full words */
    for (word++; word < FD_BITMAP_WORDS; word++) {
        if (fd_free_bitmap[word]) {
            int fd = word * 64 + bit_ctz64(fd_free_bitmap[word]);
            return (fd < FD_TABLE_SIZE) ? fd : -1;
        }
    }
    return -1;
}

/* fdtable_init. */

/* Initialize the FD table and bitmap, pre-open stdin/stdout/stderr.
 * Extracted from syscall_init(); call before any guest code runs.
 */
void fdtable_init(void)
{
    memset(fd_table, 0, sizeof(fd_table));

    /* Mark all FDs as free in bitmap */
    memset(fd_free_bitmap, 0xFF, sizeof(fd_free_bitmap));

    /* Pre-open stdin/stdout/stderr */
    fd_table[0] = (fd_entry_t) {.type = FD_STDIO, .host_fd = STDIN_FILENO};
    fd_table[1] = (fd_entry_t) {.type = FD_STDIO, .host_fd = STDOUT_FILENO};
    fd_table[2] = (fd_entry_t) {.type = FD_STDIO, .host_fd = STDERR_FILENO};
    fd_bitmap_set_used(0);
    fd_bitmap_set_used(1);
    fd_bitmap_set_used(2);
}

/* FD helpers. */

/* Find and populate the lowest free FD >= minfd. Returns -1 with errno=EMFILE
 * if no slot is available within RLIMIT_NOFILE. Caller must hold fd_lock.
 */
static int fd_alloc_locked(int minfd,
                           int type,
                           int host_fd,
                           void (*cleanup)(int))
{
    int fd = fd_bitmap_find_free(minfd);
    if (fd >= 0 && fd >= rlimit_nofile_cur)
        fd = -1; /* RLIMIT_NOFILE exceeded */
    if (fd < 0) {
        errno = EMFILE;
        return -1;
    }
    fd_init_entry(fd, type, host_fd, cleanup);
    return fd;
}

/* Allocate the lowest available FD. Returns -1 if table is full
 * or RLIMIT_NOFILE would be exceeded (sets errno to EMFILE).
 */
int fd_alloc(int type, int host_fd, void (*cleanup)(int))
{
    pthread_mutex_lock(&fd_lock);
    int fd = fd_alloc_locked(0, type, host_fd, cleanup);
    pthread_mutex_unlock(&fd_lock);
    return fd;
}

/* Allocate the lowest available FD >= minfd. Returns -1 if none available
 * or RLIMIT_NOFILE would be exceeded.
 */
int fd_alloc_from(int minfd, int type, int host_fd)
{
    pthread_mutex_lock(&fd_lock);
    int fd = fd_alloc_locked(minfd, type, host_fd, NULL);
    pthread_mutex_unlock(&fd_lock);
    return fd;
}

int fd_alloc_from_relaxed(int minfd, int type, int host_fd)
{
    if (!thread_is_single_active())
        return fd_alloc_from(minfd, type, host_fd);
    return fd_alloc_locked(minfd, type, host_fd, NULL);
}

/* Allocate a specific FD slot. Enforces RLIMIT_NOFILE. Properly cleans up any
 * existing entry (including DIR* for directory FDs) before overwriting. Returns
 * -1 if out of range.
 */
int fd_alloc_at(int fd, int type, int host_fd)
{
    if (!RANGE_CHECK(fd, 0, FD_TABLE_SIZE))
        return -1;
    if (fd >= rlimit_nofile_cur)
        return -1;

    /* Snapshot old slot state under fd_lock, then replace atomically.
     * Cleanup happens AFTER releasing fd_lock to avoid lock ordering
     * violation: cleanup functions acquire sfd_lock/inotify_lock.
     */
    fd_entry_t old = {.type = FD_CLOSED};

    pthread_mutex_lock(&fd_lock);
    if (fd_table[fd].type != FD_CLOSED)
        old = fd_table[fd];
    fd_init_entry(fd, type, host_fd, NULL);
    pthread_mutex_unlock(&fd_lock);

    /* Clean up old resources outside fd_lock */
    if (old.type != FD_CLOSED)
        fd_cleanup_entry(fd, &old);

    return fd;
}

int fd_alloc_at_relaxed(int fd, int type, int host_fd)
{
    if (!RANGE_CHECK(fd, 0, FD_TABLE_SIZE))
        return -1;
    if (fd >= rlimit_nofile_cur)
        return -1;
    if (!thread_is_single_active())
        return fd_alloc_at(fd, type, host_fd);

    if (fd_table[fd].type != FD_CLOSED)
        return fd_alloc_at(fd, type, host_fd);

    fd_init_entry(fd, type, host_fd, NULL);
    return fd;
}

/* Internal: mark fd closed with fd_lock already held.
 * Clear host_fd and dir BEFORE marking the slot free in the bitmap.
 * Otherwise another thread could fd_alloc() this slot, populate it
 * with a new host_fd/dir, and then the current stale writes would corrupt
 * the new entry.
 */
void fd_mark_closed_unlocked(int fd)
{
    fd_table[fd].type = FD_CLOSED;
    fd_table[fd].host_fd = -1;
    fd_table[fd].dir = NULL;
    fd_table[fd].proc_path[0] = '\0';
    fd_table[fd].linux_flags = 0;
    fd_table[fd].seals = 0;
    fd_bitmap_set_free(fd);
}

void fd_mark_closed(int fd)
{
    pthread_mutex_lock(&fd_lock);
    fd_mark_closed_unlocked(fd);
    pthread_mutex_unlock(&fd_lock);
}

/* Snapshot fd_table[fd] into *out and optionally close it. Caller must
 * hold fd_lock. Returns true if the slot was open, false if closed.
 */
static bool fd_snapshot_locked(int fd, fd_entry_t *out, bool close_it)
{
    if (fd_table[fd].type == FD_CLOSED)
        return false;
    *out = fd_table[fd];
    if (close_it)
        fd_mark_closed_unlocked(fd);
    return true;
}

/* Atomically snapshot an fd entry and mark it closed. Returns true if the
 * slot was open (snapshot written to *out), false if already closed. This
 * eliminates the TOCTOU race where two concurrent sys_close() calls
 * both snapshot the same open entry and double-close the host fd.
 */
bool fd_snapshot_and_close(int fd, fd_entry_t *out)
{
    if (!RANGE_CHECK(fd, 0, FD_TABLE_SIZE))
        return false;
    pthread_mutex_lock(&fd_lock);
    bool ok = fd_snapshot_locked(fd, out, true);
    pthread_mutex_unlock(&fd_lock);
    return ok;
}

bool fd_snapshot_and_close_relaxed(int fd, fd_entry_t *out)
{
    if (!RANGE_CHECK(fd, 0, FD_TABLE_SIZE))
        return false;
    if (!thread_is_single_active())
        return fd_snapshot_and_close(fd, out);
    return fd_snapshot_locked(fd, out, true);
}

bool fd_close_regular_relaxed(int fd, int *host_fd_out)
{
    if (!RANGE_CHECK(fd, 0, FD_TABLE_SIZE))
        return false;
    if (!thread_is_single_active())
        return false;

    fd_entry_t *entry = &fd_table[fd];
    if (entry->type != FD_REGULAR || entry->dir || entry->cleanup)
        return false;

    *host_fd_out = entry->host_fd;
    fd_mark_closed_unlocked(fd);
    return true;
}

/* Look up a guest FD. Returns host FD or -1 if invalid.
 * WARNING: unsafe for concurrent use; a concurrent close() can
 * invalidate the returned host fd between this call and use.
 * For race-prone paths, use fd_snapshot() or fd_to_host_dup().
 */
int fd_to_host(int guest_fd)
{
    if (!RANGE_CHECK(guest_fd, 0, FD_TABLE_SIZE))
        return -1;
    if (fd_table[guest_fd].type == FD_CLOSED)
        return -1;
    return fd_table[guest_fd].host_fd;
}

/* Snapshot an fd entry under fd_lock. Returns true if the slot was open
 * (entry copied to *out), false if closed or out of range.
 */
bool fd_snapshot(int guest_fd, fd_entry_t *out)
{
    if (!RANGE_CHECK(guest_fd, 0, FD_TABLE_SIZE))
        return false;
    pthread_mutex_lock(&fd_lock);
    bool ok = fd_snapshot_locked(guest_fd, out, false);
    pthread_mutex_unlock(&fd_lock);
    return ok;
}

/* Look up a guest FD and return a dup'd host fd that the caller owns.
 * The dup is performed under fd_lock so that close() on another thread
 * cannot invalidate the host fd between lookup and dup. Caller must
 * close the returned fd when done. Returns -1 on failure.
 */
int fd_to_host_dup(int guest_fd)
{
    if (!RANGE_CHECK(guest_fd, 0, FD_TABLE_SIZE))
        return -1;
    pthread_mutex_lock(&fd_lock);
    if (fd_table[guest_fd].type == FD_CLOSED) {
        pthread_mutex_unlock(&fd_lock);
        return -1;
    }
    int owned = dup(fd_table[guest_fd].host_fd);
    pthread_mutex_unlock(&fd_lock);
    return owned;
}

/* FD cleanup. */

/* Release all type-specific resources for a closed FD entry.
 * Caller must have already removed the entry from fd_table (via
 * fd_snapshot_and_close or fd_mark_closed). Does NOT hold fd_lock.
 *
 * This consolidates the cleanup logic that was previously duplicated
 * in sys_close, close_guest_fd_snapshot, and the execve CLOEXEC loop.
 */
void fd_cleanup_entry(int guest_fd, const fd_entry_t *snap)
{
    /* DIR* / epoll_instance_t stored in the dir field */
    if (snap->dir) {
        if (snap->type == FD_DIR)
            closedir((DIR *) snap->dir);
        else if (snap->type == FD_EPOLL)
            free(snap->dir);
    }

    /* Type-specific teardown via vtable (replaces per-type switch) */
    if (snap->cleanup)
        snap->cleanup(guest_fd);

    /* Keep stdin/stdout/stderr open on the host */
    if (snap->type != FD_STDIO)
        close(snap->host_fd);
}
