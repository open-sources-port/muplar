/* Low-address mmap hint regression test
 *
 * Copyright 2026 elfuse contributors
 * Copyright 2025 Moritz Angermann, zw3rk pte. ltd.
 * SPDX-License-Identifier: Apache-2.0
 *
 * Verifies that non-fixed anonymous mmap() honors a free low address hint
 * such as 0x400000. box64 uses this pattern to reserve the ET_EXEC image
 * window for static x86-64 binaries.
 */

#include <errno.h>
#include <stdint.h>
#include <sys/mman.h>
#include <unistd.h>

#include "test-harness.h"

#ifndef MAP_FIXED_NOREPLACE
#define MAP_FIXED_NOREPLACE 0x100000
#endif

int passes = 0, fails = 0;

static void *reserve_free_low_hint(size_t len)
{
    static const uintptr_t candidates[] = {
        0x00400000ULL, 0x00800000ULL, 0x01000000ULL,
        0x02000000ULL, 0x04000000ULL, 0x06000000ULL,
    };

    for (size_t i = 0; i < sizeof(candidates) / sizeof(candidates[0]); i++) {
        void *hint = (void *) candidates[i];
        void *p =
            mmap(hint, len, PROT_NONE,
                 MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
        if (p == MAP_FAILED) {
            if (errno == EEXIST || errno == EINVAL)
                continue;
            return MAP_FAILED;
        }
        return p;
    }

    errno = ENOMEM;
    return MAP_FAILED;
}

static void test_low_hint_exact(void)
{
    TEST("mmap low hint preserves ET_EXEC-style address");

    size_t len = 0x21000;
    void *hint = reserve_free_low_hint(len);
    if (hint == MAP_FAILED) {
        FAIL("no free low hint candidate");
        return;
    }
    munmap(hint, len);

    void *p = mmap(hint, len, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (p == MAP_FAILED) {
        FAIL("mmap failed");
        return;
    }

    EXPECT_TRUE((uintptr_t) p == (uintptr_t) hint,
                "low mmap hint should be honored when range is free");
    munmap(p, len);
}

int main(void)
{
    test_low_hint_exact();
    SUMMARY("test-mmap-hint");
    return fails ? 1 : 0;
}
