/* MADV_DONTNEED and madvise parity tests
 *
 * Copyright 2026 elfuse contributors
 * Copyright 2025 Moritz Angermann, zw3rk pte. ltd.
 * SPDX-License-Identifier: Apache-2.0
 *
 * Tests: MADV_DONTNEED zero-fill guarantee, page-aligned and multi-page ranges,
 *        advisory hints accepted, MADV_FREE on anon vs file-backed, hole
 *        detection across all advices, unknown advice rejection.
 *
 * Syscalls exercised: mmap(222), madvise(233), munmap(215)
 */

#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include "test-harness.h"

#ifndef MADV_FREE
#define MADV_FREE 8
#endif
#ifndef MADV_HUGEPAGE
#define MADV_HUGEPAGE 14
#endif
#ifndef MADV_NOHUGEPAGE
#define MADV_NOHUGEPAGE 15
#endif
#ifndef MADV_COLD
#define MADV_COLD 20
#endif
#ifndef MADV_PAGEOUT
#define MADV_PAGEOUT 21
#endif

int passes = 0, fails = 0;

/* Test 1: MADV_DONTNEED zeros a single page */

static void test_dontneed_single(void)
{
    TEST("MADV_DONTNEED single page");
    void *p = mmap(NULL, 4096, PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (p == MAP_FAILED) {
        FAIL("mmap failed");
        return;
    }

    /* Write a pattern */
    memset(p, 0xAA, 4096);

    /* MADV_DONTNEED should zero the page */
    if (madvise(p, 4096, MADV_DONTNEED) != 0) {
        FAIL("madvise failed");
        munmap(p, 4096);
        return;
    }

    /* Verify zero-fill */
    unsigned char *cp = (unsigned char *) p;
    bool ok = true;
    for (int i = 0; i < 4096; i++) {
        if (cp[i] != 0) {
            ok = false;
            break;
        }
    }

    EXPECT_TRUE(ok, "page not zeroed after MADV_DONTNEED");

    munmap(p, 4096);
}

/* Test 2: MADV_DONTNEED multi-page span */

static void test_dontneed_multi(void)
{
    TEST("MADV_DONTNEED multi-page");
    size_t sz = 4096 * 16;
    void *p = mmap(NULL, sz, PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (p == MAP_FAILED) {
        FAIL("mmap failed");
        return;
    }

    /* Fill with non-zero */
    memset(p, 0xBB, sz);

    /* DONTNEED the entire range */
    if (madvise(p, sz, MADV_DONTNEED) != 0) {
        FAIL("madvise failed");
        munmap(p, sz);
        return;
    }

    /* Verify all zeroed */
    unsigned char *cp = (unsigned char *) p;
    bool ok = true;
    for (size_t i = 0; i < sz; i++) {
        if (cp[i] != 0) {
            ok = false;
            break;
        }
    }

    EXPECT_TRUE(ok, "multi-page not zeroed");

    munmap(p, sz);
}

/* Test 3: partial range within a mapping */

static void test_dontneed_partial(void)
{
    TEST("MADV_DONTNEED partial range");
    size_t sz = 4096 * 4;
    void *p = mmap(NULL, sz, PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (p == MAP_FAILED) {
        FAIL("mmap failed");
        return;
    }

    memset(p, 0xCC, sz);

    /* Only DONTNEED pages 1-2 (leave page 0 and 3 intact) */
    if (madvise((char *) p + 4096, 4096 * 2, MADV_DONTNEED) != 0) {
        FAIL("madvise failed");
        munmap(p, sz);
        return;
    }

    unsigned char *cp = (unsigned char *) p;
    bool ok = true;

    /* Page 0: should still be 0xCC */
    for (int i = 0; i < 4096; i++) {
        if (cp[i] != 0xCC) {
            ok = false;
            break;
        }
    }
    /* Pages 1-2: should be zeroed */
    for (int i = 4096; i < 4096 * 3; i++) {
        if (cp[i] != 0) {
            ok = false;
            break;
        }
    }
    /* Page 3: should still be 0xCC */
    for (int i = 4096 * 3; i < (int) sz; i++) {
        if (cp[i] != 0xCC) {
            ok = false;
            break;
        }
    }

    EXPECT_TRUE(ok, "partial DONTNEED corrupted adjacent pages");

    munmap(p, sz);
}

/* Test 4: write-after-DONTNEED works */

static void test_dontneed_rewrite(void)
{
    TEST("MADV_DONTNEED then rewrite");
    void *p = mmap(NULL, 4096, PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (p == MAP_FAILED) {
        FAIL("mmap failed");
        return;
    }

    memset(p, 0xDD, 4096);
    madvise(p, 4096, MADV_DONTNEED);

    /* Write new data after DONTNEED */
    memset(p, 0xEE, 4096);

    unsigned char *cp = (unsigned char *) p;
    bool ok = true;
    for (int i = 0; i < 4096; i++) {
        if (cp[i] != 0xEE) {
            ok = false;
            break;
        }
    }

    EXPECT_TRUE(ok, "rewrite after DONTNEED failed");

    munmap(p, 4096);
}

/* Test 5: advisory hints accepted */

static void test_advisory_hints(void)
{
    TEST("madvise advisory hints");
    void *p = mmap(NULL, 4096, PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (p == MAP_FAILED) {
        FAIL("mmap failed");
        return;
    }

    bool ok = true;
    if (madvise(p, 4096, MADV_NORMAL) != 0)
        ok = false;
    if (madvise(p, 4096, MADV_RANDOM) != 0)
        ok = false;
    if (madvise(p, 4096, MADV_SEQUENTIAL) != 0)
        ok = false;
    if (madvise(p, 4096, MADV_WILLNEED) != 0)
        ok = false;

    EXPECT_TRUE(ok, "advisory hint rejected");

    munmap(p, 4096);
}

/* Test 6: large MADV_DONTNEED (jemalloc pattern) */

static void test_dontneed_large(void)
{
    TEST("MADV_DONTNEED 1MiB range");
    size_t sz = 1024 * 1024;
    void *p = mmap(NULL, sz, PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (p == MAP_FAILED) {
        FAIL("mmap failed");
        return;
    }

    memset(p, 0xFF, sz);

    if (madvise(p, sz, MADV_DONTNEED) != 0) {
        FAIL("madvise failed");
        munmap(p, sz);
        return;
    }

    /* Spot-check zeroed */
    unsigned char *cp = (unsigned char *) p;
    bool ok = true;
    for (size_t i = 0; i < sz; i += 4096) {
        if (cp[i] != 0) {
            ok = false;
            break;
        }
    }

    EXPECT_TRUE(ok, "1MiB range not zeroed");

    munmap(p, sz);
}

/* Test 7: unaligned address rejected */

static void test_dontneed_unaligned(void)
{
    TEST("madvise unaligned addr rejected");
    void *p = mmap(NULL, 4096 * 2, PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (p == MAP_FAILED) {
        FAIL("mmap failed");
        return;
    }

    /* Unaligned address should fail */
    int ret = madvise((char *) p + 1, 4096, MADV_DONTNEED);
    EXPECT_TRUE(ret != 0, "should reject unaligned address");

    munmap(p, 4096 * 2);
}

/* Test 8: file-backed private mappings keep file contents */

static void test_dontneed_file_backed(void)
{
    TEST("MADV_DONTNEED file-backed mapping");

    char template[] = "/tmp/elfuse-madvise-XXXXXX";
    int fd = mkstemp(template);
    if (fd < 0) {
        FAIL("mkstemp failed");
        return;
    }
    unlink(template);

    unsigned char pattern[4096];
    memset(pattern, 0x5A, sizeof(pattern));
    if (write(fd, pattern, sizeof(pattern)) != (ssize_t) sizeof(pattern)) {
        FAIL("write failed");
        close(fd);
        return;
    }

    void *p = mmap(NULL, 4096, PROT_READ, MAP_PRIVATE, fd, 0);
    if (p == MAP_FAILED) {
        FAIL("mmap file failed");
        close(fd);
        return;
    }

    if (madvise(p, 4096, MADV_DONTNEED) != 0) {
        FAIL("madvise failed");
        munmap(p, 4096);
        close(fd);
        return;
    }

    unsigned char *cp = (unsigned char *) p;
    bool ok = true;
    for (int i = 0; i < 4096; i++) {
        if (cp[i] != 0x5A) {
            ok = false;
            break;
        }
    }

    EXPECT_TRUE(ok, "file-backed contents were zeroed");

    munmap(p, 4096);
    close(fd);
}

/* Test 9: MADV_DONTNEED across an unmapped hole returns ENOMEM */

static void test_dontneed_hole(void)
{
    TEST("MADV_DONTNEED across unmapped hole");
    void *p = mmap(NULL, 4096 * 3, PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (p == MAP_FAILED) {
        FAIL("mmap failed");
        return;
    }

    /* Punch out the middle page so [p, p+12k) covers a hole */
    if (munmap((char *) p + 4096, 4096) != 0) {
        FAIL("munmap failed");
        return;
    }

    errno = 0;
    int rc = madvise(p, 4096 * 3, MADV_DONTNEED);
    EXPECT_TRUE(rc < 0 && errno == ENOMEM, "expected ENOMEM for hole");

    munmap(p, 4096);
    munmap((char *) p + 4096 * 2, 4096);
}

/* Test 10: MADV_FREE on anonymous private mapping */

static void test_free_anon(void)
{
    TEST("MADV_FREE on anonymous mapping");
    void *p = mmap(NULL, 4096, PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (p == MAP_FAILED) {
        FAIL("mmap failed");
        return;
    }

    memset(p, 0x77, 4096);
    int rc = madvise(p, 4096, MADV_FREE);

    /* Per spec the read may return either old data or zero, so we only
     * check the syscall succeeds and a write/read round-trip works.
     */
    if (rc != 0) {
        FAIL("madvise MADV_FREE rejected anon mapping");
        munmap(p, 4096);
        return;
    }

    /* Subsequent write must stick */
    memset(p, 0x88, 4096);
    unsigned char *cp = (unsigned char *) p;
    bool ok = true;
    for (int i = 0; i < 4096; i++) {
        if (cp[i] != 0x88) {
            ok = false;
            break;
        }
    }
    EXPECT_TRUE(ok, "write after MADV_FREE did not persist");

    munmap(p, 4096);
}

/* Test 11: MADV_FREE on file-backed mapping returns EINVAL */

static void test_free_file_backed(void)
{
    TEST("MADV_FREE rejects file-backed mapping");

    char template[] = "/tmp/elfuse-madv-free-XXXXXX";
    int fd = mkstemp(template);
    if (fd < 0) {
        FAIL("mkstemp failed");
        return;
    }
    unlink(template);

    char buf[4096];
    memset(buf, 0x11, sizeof(buf));
    if (write(fd, buf, sizeof(buf)) != (ssize_t) sizeof(buf)) {
        FAIL("write");
        close(fd);
        return;
    }

    void *p = mmap(NULL, 4096, PROT_READ, MAP_PRIVATE, fd, 0);
    if (p == MAP_FAILED) {
        FAIL("mmap file");
        close(fd);
        return;
    }

    errno = 0;
    int rc = madvise(p, 4096, MADV_FREE);
    EXPECT_TRUE(rc < 0 && errno == EINVAL,
                "expected EINVAL for file-backed MADV_FREE");

    munmap(p, 4096);
    close(fd);
}

/* Test 12: MADV_FREE rejects private file mappings after close(fd) */

static void test_free_file_backed_closed_fd(void)
{
    TEST("MADV_FREE rejects closed-fd file mapping");

    char template[] = "/tmp/elfuse-madv-free-closed-XXXXXX";
    int fd = mkstemp(template);
    if (fd < 0) {
        FAIL("mkstemp failed");
        return;
    }
    unlink(template);

    char buf[4096];
    memset(buf, 0x22, sizeof(buf));
    if (write(fd, buf, sizeof(buf)) != (ssize_t) sizeof(buf)) {
        FAIL("write");
        close(fd);
        return;
    }

    void *p = mmap(NULL, 4096, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    if (p == MAP_FAILED) {
        FAIL("mmap file");
        return;
    }

    errno = 0;
    int rc = madvise(p, 4096, MADV_FREE);
    EXPECT_TRUE(rc < 0 && errno == EINVAL,
                "expected EINVAL for closed-fd file-backed MADV_FREE");

    munmap(p, 4096);
}

/* Test 13: MADV_HUGEPAGE / MADV_NOHUGEPAGE / MADV_COLD / MADV_PAGEOUT */

static void test_extra_hints(void)
{
    TEST("madvise hugepage/cold/pageout hints");
    void *p = mmap(NULL, 4096, PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (p == MAP_FAILED) {
        FAIL("mmap");
        return;
    }

    bool ok = true;
    if (madvise(p, 4096, MADV_HUGEPAGE) != 0)
        ok = false;
    if (madvise(p, 4096, MADV_NOHUGEPAGE) != 0)
        ok = false;
    if (madvise(p, 4096, MADV_COLD) != 0)
        ok = false;
    if (madvise(p, 4096, MADV_PAGEOUT) != 0)
        ok = false;

    EXPECT_TRUE(ok, "hint advice rejected");

    /* Data must survive PAGEOUT/COLD on this no-swap host */
    memset(p, 0x99, 4096);
    if (madvise(p, 4096, MADV_PAGEOUT) != 0)
        ok = false;
    unsigned char *cp = (unsigned char *) p;
    for (int i = 0; i < 4096; i++) {
        if (cp[i] != 0x99) {
            ok = false;
            break;
        }
    }
    EXPECT_TRUE(ok, "data did not survive MADV_PAGEOUT");

    munmap(p, 4096);
}

/* Test 13: hint advices on a hole return ENOMEM (Linux parity) */

static void test_hint_hole(void)
{
    TEST("madvise hint on hole returns ENOMEM");
    void *p = mmap(NULL, 4096 * 3, PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (p == MAP_FAILED) {
        FAIL("mmap");
        return;
    }
    munmap((char *) p + 4096, 4096);

    errno = 0;
    int rc = madvise(p, 4096 * 3, MADV_WILLNEED);
    EXPECT_TRUE(rc < 0 && errno == ENOMEM, "expected ENOMEM for WILLNEED hole");

    munmap(p, 4096);
    munmap((char *) p + 4096 * 2, 4096);
}

/* Test 14: unknown advice value rejected */

static void test_unknown_advice(void)
{
    TEST("madvise unknown advice returns EINVAL");
    void *p = mmap(NULL, 4096, PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (p == MAP_FAILED) {
        FAIL("mmap");
        return;
    }

    errno = 0;
    /* 9999 is not a defined advice */
    int rc = madvise(p, 4096, 9999);
    EXPECT_TRUE(rc < 0 && errno == EINVAL,
                "expected EINVAL for unknown advice");

    munmap(p, 4096);
}

/* Test 15: length=0 succeeds without error */

static void test_zero_length(void)
{
    TEST("madvise length=0 returns 0");
    void *p = mmap(NULL, 4096, PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (p == MAP_FAILED) {
        FAIL("mmap");
        return;
    }

    int rc = madvise(p, 0, MADV_DONTNEED);
    EXPECT_TRUE(rc == 0, "length=0 should succeed");

    munmap(p, 4096);
}

/* Test 16: PROT_NONE region zero-fills on subsequent re-grant. */

static void test_dontneed_prot_none_zerofill(void)
{
    TEST("MADV_DONTNEED through PROT_NONE round trip");
    void *p = mmap(NULL, 4096, PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (p == MAP_FAILED) {
        FAIL("mmap");
        return;
    }

    memset(p, 0x42, 4096);

    if (mprotect(p, 4096, PROT_NONE) != 0) {
        FAIL("mprotect PROT_NONE");
        munmap(p, 4096);
        return;
    }

    if (madvise(p, 4096, MADV_DONTNEED) != 0) {
        FAIL("madvise");
        munmap(p, 4096);
        return;
    }

    if (mprotect(p, 4096, PROT_READ | PROT_WRITE) != 0) {
        FAIL("mprotect restore");
        munmap(p, 4096);
        return;
    }

    unsigned char *cp = (unsigned char *) p;
    bool ok = true;
    for (int i = 0; i < 4096; i++) {
        if (cp[i] != 0) {
            ok = false;
            break;
        }
    }
    EXPECT_TRUE(ok, "PROT_NONE region not zeroed after restore");

    munmap(p, 4096);
}

/* Test 17: MADV_DONTNEED on MAP_SHARED file mapping preserves dirty data.
 *
 * Linux cannot discard pages from a shared page-cache mapping; in elfuse's
 * MAP_SHARED-as-CoW model, an in-memory write must not be silently
 * overwritten by stale file contents during DONTNEED.
 */

static void test_dontneed_shared_file_preserved(void)
{
    TEST("MADV_DONTNEED MAP_SHARED preserves in-memory writes");

    char template[] = "/tmp/elfuse-madv-shared-XXXXXX";
    int fd = mkstemp(template);
    if (fd < 0) {
        FAIL("mkstemp");
        return;
    }
    unlink(template);

    char buf[4096];
    memset(buf, 0x11, sizeof(buf));
    if (write(fd, buf, sizeof(buf)) != (ssize_t) sizeof(buf)) {
        FAIL("write");
        close(fd);
        return;
    }

    void *p = mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (p == MAP_FAILED) {
        FAIL("mmap shared");
        close(fd);
        return;
    }

    /* Dirty the in-memory copy without msync. */
    memset(p, 0x33, 4096);

    if (madvise(p, 4096, MADV_DONTNEED) != 0) {
        FAIL("madvise");
        munmap(p, 4096);
        close(fd);
        return;
    }

    unsigned char *cp = (unsigned char *) p;
    bool ok = true;
    for (int i = 0; i < 4096; i++) {
        if (cp[i] != 0x33) {
            ok = false;
            break;
        }
    }
    EXPECT_TRUE(ok, "MAP_SHARED dirty data lost after DONTNEED");

    munmap(p, 4096);
    close(fd);
}

/* Test 18: MADV_DONTNEED on clean read-only MAP_SHARED refaults from file. */

static void test_dontneed_shared_file_reload(void)
{
    TEST("MADV_DONTNEED MAP_SHARED reloads clean file data");

    char template[] = "/tmp/elfuse-madv-shared-reload-XXXXXX";
    int fd = mkstemp(template);
    if (fd < 0) {
        FAIL("mkstemp");
        return;
    }
    unlink(template);

    char buf[4096];
    memset(buf, 0x44, sizeof(buf));
    if (write(fd, buf, sizeof(buf)) != (ssize_t) sizeof(buf)) {
        FAIL("write");
        close(fd);
        return;
    }

    void *p = mmap(NULL, 4096, PROT_READ, MAP_SHARED, fd, 0);
    if (p == MAP_FAILED) {
        FAIL("mmap shared");
        close(fd);
        return;
    }

    memset(buf, 0x66, sizeof(buf));
    if (pwrite(fd, buf, sizeof(buf), 0) != (ssize_t) sizeof(buf)) {
        FAIL("pwrite");
        munmap(p, 4096);
        close(fd);
        return;
    }

    if (madvise(p, 4096, MADV_DONTNEED) != 0) {
        FAIL("madvise");
        munmap(p, 4096);
        close(fd);
        return;
    }

    unsigned char *cp = (unsigned char *) p;
    bool ok = true;
    for (int i = 0; i < 4096; i++) {
        if (cp[i] != 0x66) {
            ok = false;
            break;
        }
    }
    EXPECT_TRUE(ok, "MAP_SHARED clean data did not reload from file");

    munmap(p, 4096);
    close(fd);
}

/* Test 19: MADV_FREE rejects shared anonymous mappings. */

static void test_free_shared_anon(void)
{
    TEST("MADV_FREE rejects shared anonymous mapping");

    void *p = mmap(NULL, 4096, PROT_READ | PROT_WRITE,
                   MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    if (p == MAP_FAILED) {
        FAIL("mmap shared anon");
        return;
    }

    errno = 0;
    int rc = madvise(p, 4096, MADV_FREE);
    EXPECT_TRUE(rc < 0 && errno == EINVAL,
                "expected EINVAL for MAP_SHARED|MAP_ANONYMOUS MADV_FREE");

    munmap(p, 4096);
}

/* Test 20: address outside the guest address space returns ENOMEM.
 *
 * Per madvise(2), addresses outside the process address space yield
 * ENOMEM, not EINVAL. The high half of the 64-bit space is well past
 * any IPA window elfuse can be configured with (1 TiB ceiling for
 * 40-bit IPA, 64 GiB for 36-bit), so this address is unconditionally
 * out of range.
 */

static void test_oob_address_enomem(void)
{
    TEST("madvise OOB address returns ENOMEM");
    void *p = (void *) 0xFFFFFFFFFFFF0000ULL;
    errno = 0;
    int rc = madvise(p, 4096, MADV_DONTNEED);
    EXPECT_TRUE(rc < 0 && errno == ENOMEM,
                "expected ENOMEM for address beyond guest space");
}

int main(void)
{
    printf("test-madvise: MADV_DONTNEED and parity tests\n");

    test_dontneed_single();
    test_dontneed_multi();
    test_dontneed_partial();
    test_dontneed_rewrite();
    test_advisory_hints();
    test_dontneed_large();
    test_dontneed_unaligned();
    test_dontneed_file_backed();
    test_dontneed_hole();
    test_free_anon();
    test_free_file_backed();
    test_free_file_backed_closed_fd();
    test_extra_hints();
    test_hint_hole();
    test_unknown_advice();
    test_zero_length();
    test_dontneed_prot_none_zerofill();
    test_dontneed_shared_file_preserved();
    test_dontneed_shared_file_reload();
    test_free_shared_anon();
    test_oob_address_enomem();

    SUMMARY("test-madvise");
    return fails > 0 ? 1 : 0;
}
