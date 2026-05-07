/* MAP_SHARED msync tests
 *
 * Copyright 2026 elfuse contributors
 * Copyright 2025 Moritz Angermann, zw3rk pte. ltd.
 * SPDX-License-Identifier: Apache-2.0
 *
 * Tests: MAP_SHARED write-back to shm backing files.
 *
 * Syscalls exercised: shm_open/openat, ftruncate, mmap, msync, pread64
 */

#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>

#include "test-harness.h"

int passes = 0, fails = 0;

/* glibc 2.28 static: shm_open is broken (returns ENOSYS without trying).
 * Implement directly via openat on /dev/shm/.
 */
static int my_shm_open(const char *name, int oflag, int mode)
{
    char path[128];
    snprintf(path, sizeof(path), "/dev/shm%s", name);
    return open(path, oflag, mode);
}

static int my_shm_unlink(const char *name)
{
    char path[128];
    snprintf(path, sizeof(path), "/dev/shm%s", name);
    return unlink(path);
}

static void test_shared_msync_writes_file(void)
{
    TEST("MAP_SHARED msync writes backing file");

    char name[64];
    snprintf(name, sizeof(name), "/elfuse-msync-%ld", (long) getpid());
    int fd = my_shm_open(name, O_CREAT | O_EXCL | O_RDWR, 0600);
    if (fd < 0) {
        FAIL("shm_open failed");
        return;
    }
    my_shm_unlink(name);

    if (ftruncate(fd, 4096) != 0) {
        FAIL("ftruncate failed");
        close(fd);
        return;
    }

    char *p = mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (p == MAP_FAILED) {
        FAIL("mmap failed");
        close(fd);
        return;
    }

    const char msg[] = "elfuse-msync";
    memcpy(p, msg, sizeof(msg));

    if (msync(p, 4096, MS_SYNC) != 0) {
        FAIL("msync failed");
        munmap(p, 4096);
        close(fd);
        return;
    }

    char buf[sizeof(msg)] = {0};
    if (pread(fd, buf, sizeof(buf), 0) != (ssize_t) sizeof(buf)) {
        FAIL("pread failed");
    } else if (!memcmp(buf, msg, sizeof(msg))) {
        PASS();
    } else {
        FAIL("backing file did not receive MAP_SHARED writes");
    }

    munmap(p, 4096);
    close(fd);
}

static void test_shared_msync_refreshes_peer_mapping(void)
{
    TEST("MAP_SHARED msync refreshes peer mapping");

    char name[64];
    snprintf(name, sizeof(name), "/elfuse-msync-peer-%ld", (long) getpid());
    int fd = my_shm_open(name, O_CREAT | O_EXCL | O_RDWR, 0600);
    if (fd < 0) {
        FAIL("shm_open failed");
        return;
    }
    my_shm_unlink(name);

    if (ftruncate(fd, 4096) != 0) {
        FAIL("ftruncate failed");
        close(fd);
        return;
    }

    char *a = mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    char *b = mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (a == MAP_FAILED || b == MAP_FAILED) {
        FAIL("mmap failed");
        if (a != MAP_FAILED)
            munmap(a, 4096);
        if (b != MAP_FAILED)
            munmap(b, 4096);
        close(fd);
        return;
    }

    a[128] = 'Q';
    if (msync(a, 4096, MS_SYNC) != 0) {
        FAIL("msync failed");
    } else if (b[128] == 'Q') {
        PASS();
    } else {
        FAIL("peer MAP_SHARED mapping did not observe write after msync");
    }

    munmap(a, 4096);
    munmap(b, 4096);
    close(fd);
}

static void test_shared_msync_preserves_alias_writes(void)
{
    TEST("MAP_SHARED msync preserves peer alias writes");

    char name[64];
    snprintf(name, sizeof(name), "/elfuse-msync-alias-%ld", (long) getpid());
    int fd = my_shm_open(name, O_CREAT | O_EXCL | O_RDWR, 0600);
    if (fd < 0) {
        FAIL("shm_open failed");
        return;
    }
    my_shm_unlink(name);

    if (ftruncate(fd, 4096) != 0) {
        FAIL("ftruncate failed");
        close(fd);
        return;
    }

    char *a = mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    char *b = mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (a == MAP_FAILED || b == MAP_FAILED) {
        FAIL("mmap failed");
        if (a != MAP_FAILED)
            munmap(a, 4096);
        if (b != MAP_FAILED)
            munmap(b, 4096);
        close(fd);
        return;
    }

    a[0] = 'A';
    b[1] = 'B';

    if (msync(a, 4096, MS_SYNC) != 0) {
        FAIL("msync failed");
    } else {
        char buf[2] = {0};
        if (pread(fd, buf, sizeof(buf), 0) != (ssize_t) sizeof(buf))
            FAIL("pread failed");
        else if (buf[0] == 'A' && buf[1] == 'B' && a[1] == 'B')
            PASS();
        else
            FAIL("msync lost dirty bytes from peer alias");
    }

    munmap(a, 4096);
    munmap(b, 4096);
    close(fd);
}

static void test_shm_name_visible_after_fork(void)
{
    TEST("/dev/shm name visible after fork");

    char name[64];
    snprintf(name, sizeof(name), "/elfuse-msync-fork-%ld", (long) getpid());
    int fd = my_shm_open(name, O_CREAT | O_EXCL | O_RDWR, 0600);
    if (fd < 0) {
        FAIL("parent shm_open failed");
        return;
    }

    if (ftruncate(fd, 4096) != 0) {
        FAIL("ftruncate failed");
        my_shm_unlink(name);
        close(fd);
        return;
    }

    char *p = mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (p == MAP_FAILED) {
        FAIL("parent mmap failed");
        my_shm_unlink(name);
        close(fd);
        return;
    }
    p[0] = 'P';
    msync(p, 4096, MS_SYNC);

    pid_t child = fork();
    if (child < 0) {
        FAIL("fork failed");
    } else if (child == 0) {
        int cfd = my_shm_open(name, O_RDWR, 0600);
        if (cfd < 0)
            _exit(2);
        char *cp = mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_SHARED, cfd, 0);
        if (cp == MAP_FAILED)
            _exit(3);
        int ok = cp[0] == 'P';
        if (ok) {
            cp[1] = 'C';
            ok = msync(cp, 4096, MS_SYNC) == 0;
        }
        _exit(ok ? 0 : 4);
    } else {
        int status;
        if (waitpid(child, &status, 0) < 0) {
            FAIL("waitpid failed");
        } else if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
            FAIL("child could not reopen shared shm name");
        } else {
            char buf[2] = {0};
            if (pread(fd, buf, sizeof(buf), 0) != (ssize_t) sizeof(buf))
                FAIL("parent pread failed");
            else if (buf[1] == 'C')
                PASS();
            else
                FAIL("parent did not observe child shm write");
        }
    }

    munmap(p, 4096);
    close(fd);
    my_shm_unlink(name);
}

/* Real MAP_SHARED requires that host writes to the backing file are
 * observable through the mapping without the guest calling msync. The
 * pre-overlay implementation snapshotted file contents into private
 * guest pages and only reconciled on msync, so this is the regression
 * lock-in for the overlay path.
 */
static void test_shared_host_write_visible_without_msync(void)
{
    TEST("MAP_SHARED host pwrite visible without msync");

    char name[64];
    snprintf(name, sizeof(name), "/elfuse-msync-host-%ld", (long) getpid());
    int fd = my_shm_open(name, O_CREAT | O_EXCL | O_RDWR, 0600);
    if (fd < 0) {
        FAIL("shm_open failed");
        return;
    }
    my_shm_unlink(name);

    if (ftruncate(fd, 4096) != 0) {
        FAIL("ftruncate failed");
        close(fd);
        return;
    }

    /* Seed the file with a known pattern before mmap. */
    char seed[16];
    memset(seed, 0x11, sizeof(seed));
    if (pwrite(fd, seed, sizeof(seed), 0) != (ssize_t) sizeof(seed)) {
        FAIL("seed pwrite failed");
        close(fd);
        return;
    }

    char *p = mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (p == MAP_FAILED) {
        FAIL("mmap failed");
        close(fd);
        return;
    }

    /* Confirm the initial seed is visible through the mapping. */
    if (p[0] != 0x11) {
        FAIL("initial seed not visible through mapping");
        munmap(p, 4096);
        close(fd);
        return;
    }

    /* Mutate the file via pwrite (host-side write). The mapping must
     * reflect the new bytes immediately, with no msync from the guest.
     */
    char update[16];
    memset(update, 0x22, sizeof(update));
    if (pwrite(fd, update, sizeof(update), 0) != (ssize_t) sizeof(update)) {
        FAIL("update pwrite failed");
        munmap(p, 4096);
        close(fd);
        return;
    }

    bool ok = true;
    for (int i = 0; i < (int) sizeof(update); i++) {
        if ((unsigned char) p[i] != 0x22) {
            ok = false;
            break;
        }
    }
    if (ok)
        PASS();
    else
        FAIL("mapping did not reflect host pwrite without msync");

    munmap(p, 4096);
    close(fd);
}

/* Guest writes to a MAP_SHARED file mapping must reach the file
 * immediately so other readers (here, a sibling pread) see them without
 * the guest needing to call msync. This is the converse of the
 * host-write-visible test.
 */
static void test_shared_guest_write_lands_in_file(void)
{
    TEST("MAP_SHARED guest write lands in file without msync");

    char name[64];
    snprintf(name, sizeof(name), "/elfuse-msync-guest-%ld", (long) getpid());
    int fd = my_shm_open(name, O_CREAT | O_EXCL | O_RDWR, 0600);
    if (fd < 0) {
        FAIL("shm_open failed");
        return;
    }
    my_shm_unlink(name);

    if (ftruncate(fd, 4096) != 0) {
        FAIL("ftruncate failed");
        close(fd);
        return;
    }

    char *p = mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (p == MAP_FAILED) {
        FAIL("mmap failed");
        close(fd);
        return;
    }

    /* Write through the mapping; do NOT call msync. */
    const char marker[] = "elfuse-overlay";
    memcpy(p, marker, sizeof(marker));

    char buf[sizeof(marker)] = {0};
    if (pread(fd, buf, sizeof(buf), 0) != (ssize_t) sizeof(buf))
        FAIL("pread failed");
    else if (!memcmp(buf, marker, sizeof(marker)))
        PASS();
    else
        FAIL("guest write did not reach file without msync");

    munmap(p, 4096);
    close(fd);
}

/* On hosts with pages larger than the guest's 4 KiB granule, a MAP_SHARED
 * overlay of one guest page can alias adjacent guest pages in the same host
 * page. Replacing the adjacent guest page must tear down the shared overlay
 * first so the new mapping cannot write through into the file.
 */
static void test_shared_adjacent_fixed_mapping_does_not_alias_file(void)
{
    TEST("MAP_FIXED neighbor does not inherit shared-file overlay");

    size_t hps = (size_t) sysconf(_SC_PAGESIZE);
    size_t file_len = hps > 8192 ? hps : 8192;

    char name[64];
    snprintf(name, sizeof(name), "/elfuse-msync-alias-%ld", (long) getpid());
    int fd = my_shm_open(name, O_CREAT | O_EXCL | O_RDWR, 0600);
    if (fd < 0) {
        FAIL("shm_open failed");
        return;
    }
    my_shm_unlink(name);

    if (ftruncate(fd, (off_t) file_len) != 0) {
        FAIL("ftruncate failed");
        close(fd);
        return;
    }

    unsigned char seed = 0x33;
    if (pwrite(fd, &seed, 1, 4096) != 1) {
        FAIL("seed pwrite failed");
        close(fd);
        return;
    }

    unsigned char *shared =
        mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (shared == MAP_FAILED) {
        FAIL("shared mmap failed");
        close(fd);
        return;
    }

    unsigned char *adjacent =
        mmap(shared + 4096, 4096, PROT_READ | PROT_WRITE,
             MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
    if (adjacent == MAP_FAILED) {
        FAIL("adjacent MAP_FIXED mmap failed");
        munmap(shared, 4096);
        close(fd);
        return;
    }

    adjacent[0] = 0x7a;

    unsigned char file_byte = 0;
    if (pread(fd, &file_byte, 1, 4096) != 1)
        FAIL("pread failed");
    else if (file_byte == seed)
        PASS();
    else
        FAIL("adjacent anonymous write leaked into file");

    munmap(shared, 4096);
    munmap(adjacent, 4096);
    close(fd);
}

int main(void)
{
    printf("test-msync: MAP_SHARED msync tests\n\n");

    test_shared_msync_writes_file();
    test_shared_msync_refreshes_peer_mapping();
    test_shared_msync_preserves_alias_writes();
    test_shared_host_write_visible_without_msync();
    test_shared_guest_write_lands_in_file();
    test_shared_adjacent_fixed_mapping_does_not_alias_file();
    test_shm_name_visible_after_fork();

    SUMMARY("test-msync");
    return fails ? 1 : 0;
}
