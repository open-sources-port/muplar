/* Stack guard page and mmap edge case tests
 *
 * Copyright 2026 elfuse contributors
 * Copyright 2025 Moritz Angermann, zw3rk pte. ltd.
 * SPDX-License-Identifier: Apache-2.0
 *
 * Tests: stack guard page (PROT_NONE), large mmap allocations,
 *        MAP_FIXED overlap, mprotect transitions.
 *
 * Syscalls exercised: mmap(222), munmap(215), mprotect(226)
 */

#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include "test-harness.h"

int passes = 0, fails = 0;

/* Test 1: mmap PROT_NONE is inaccessible */

static void test_prot_none(void)
{
    TEST("mmap PROT_NONE succeeds");

    void *p = mmap(NULL, 4096, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (p != MAP_FAILED) {
        PASS();
        munmap(p, 4096);
    } else {
        FAIL("mmap PROT_NONE failed");
    }

    TEST("mprotect NONE->RW->NONE cycle");

    p = mmap(NULL, 4096, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (p == MAP_FAILED) {
        FAIL("mmap failed");
        return;
    }

    /* Upgrade to RW, write, verify */
    if (mprotect(p, 4096, PROT_READ | PROT_WRITE) != 0) {
        FAIL("mprotect NONE->RW failed");
        munmap(p, 4096);
        return;
    }
    *(volatile int *) p = 0xDEADBEEF;
    EXPECT_TRUE(*(volatile int *) p == (int) 0xDEADBEEF,
                "data not readable after RW mprotect");

    /* Downgrade back to NONE (do not access; would segfault) */
    mprotect(p, 4096, PROT_NONE);
    munmap(p, 4096);
}

/* Test 2: Large mmap allocation */

static void test_large_mmap(void)
{
    TEST("mmap 64MiB anonymous");

    size_t sz = 64UL * 1024 * 1024;
    void *p = mmap(NULL, sz, PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (p == MAP_FAILED) {
        FAIL("mmap 64MiB failed");
        return;
    }

    /* Touch first, middle, and last pages to verify demand paging */
    volatile char *c = (volatile char *) p;
    c[0] = 'A';
    c[sz / 2] = 'B';
    c[sz - 1] = 'C';

    EXPECT_TRUE(c[0] == 'A' && c[sz / 2] == 'B' && c[sz - 1] == 'C',
                "data mismatch in 64MiB region");

    munmap(p, sz);
}

/* Test 3: MAP_FIXED overlapping */

static void test_map_fixed_overlap(void)
{
    TEST("MAP_FIXED replaces existing");

    size_t sz = 3 * 4096;
    void *p = mmap(NULL, sz, PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (p == MAP_FAILED) {
        FAIL("initial mmap failed");
        return;
    }

    volatile int *mid = (volatile int *) ((char *) p + 4096);
    *mid = 0x12345678;

    /* MAP_FIXED over the middle page with a new mapping */
    void *p2 = mmap((char *) p + 4096, 4096, PROT_READ | PROT_WRITE,
                    MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
    if (p2 == MAP_FAILED) {
        FAIL("MAP_FIXED failed");
        munmap(p, sz);
        return;
    }

    /* New mapping should be zeroed (fresh anonymous page) */
    EXPECT_TRUE(*mid == 0, "MAP_FIXED didn't zero replaced page");

    munmap(p, sz);
}

/* Test 4: mprotect sub-page granularity */

static void test_mprotect_partial(void)
{
    TEST("mprotect partial region");

    size_t sz = 4 * 4096;
    void *p = mmap(NULL, sz, PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (p == MAP_FAILED) {
        FAIL("mmap failed");
        return;
    }

    for (int i = 0; i < 4; i++)
        ((volatile int *) ((char *) p + i * 4096))[0] = i + 1;

    /* Make middle 2 pages read-only */
    if (mprotect((char *) p + 4096, 2 * 4096, PROT_READ) != 0) {
        FAIL("mprotect failed");
        munmap(p, sz);
        return;
    }

    /* First and last pages should still be writable */
    ((volatile int *) p)[0] = 99;
    ((volatile int *) ((char *) p + 3 * 4096))[0] = 99;

    /* Middle pages should still be readable */
    int v1 = ((volatile int *) ((char *) p + 4096))[0];
    int v2 = ((volatile int *) ((char *) p + 2 * 4096))[0];
    EXPECT_TRUE(v1 == 2 && v2 == 3, "middle page data corrupted");

    munmap(p, sz);
}

/* Test 5: Multiple mmap/munmap cycles */

static void test_mmap_churn(void)
{
    TEST("100x mmap/munmap cycle");

    bool ok = true;
    for (int i = 0; i < 100; i++) {
        void *p = mmap(NULL, 4096, PROT_READ | PROT_WRITE,
                       MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (p == MAP_FAILED) {
            ok = false;
            break;
        }
        *(volatile int *) p = i;
        volatile int value = *(volatile int *) p;
        if (value != i) {
            ok = false;
            break;
        }
        munmap(p, 4096);
    }
    EXPECT_TRUE(ok, "mmap/munmap cycle failed");
}

/* Main */

int main(void)
{
    printf("test-guard-page: stack guard + mmap edge case tests\n");

    test_prot_none();
    test_large_mmap();
    test_map_fixed_overlap();
    test_mprotect_partial();
    test_mmap_churn();

    SUMMARY("test-guard-page");
    return fails > 0 ? 1 : 0;
}
