/* /proc/self/{auxv,environ} exec refresh regression test
 *
 * Copyright 2026 elfuse contributors
 * SPDX-License-Identifier: Apache-2.0
 *
 * Verifies that:
 *   1. /proc/self/auxv contains AT_EXECFN, AT_RANDOM, and AT_HWCAP
 *   2. /proc/self/auxv matches getauxval() after execve()
 *   3. /proc/self/environ reflects the replacement image after execve()
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/auxv.h>

#include "test-harness.h"
#include "test-util.h"

int passes = 0, fails = 0;

#define AT_NULL 0
#define AT_HWCAP 16
#define AT_RANDOM 25
#define AT_EXECFN 31

static bool read_proc_auxv(uint64_t *buf, size_t words, ssize_t *nbytes_out)
{
    ssize_t n = raw_read_file_nul("/proc/self/auxv", (char *) buf,
                                  words * sizeof(*buf));
    if (n < 0 || (n & 7) != 0)
        return false;
    *nbytes_out = n;
    return true;
}

static bool find_auxv_word(const uint64_t *buf,
                           size_t nwords,
                           uint64_t key,
                           uint64_t *value_out)
{
    for (size_t i = 0; i + 1 < nwords; i += 2) {
        if (buf[i] == key) {
            if (value_out)
                *value_out = buf[i + 1];
            return true;
        }
        if (buf[i] == AT_NULL)
            break;
    }
    return false;
}

static void run_checks(void)
{
    uint64_t auxv[128];
    ssize_t nbytes;
    char buf[4096];
    const char expect_env[] = "ELFUSE_PROCFS_AFTER=1";
    uint64_t proc_execfn = 0, proc_random = 0, proc_hwcap = 0;

    TEST("procfs exec: /proc/self/auxv readable");
    if (!read_proc_auxv(auxv, 128, &nbytes)) {
        FAIL("could not read auxv");
        return;
    }
    PASS();

    TEST("procfs exec: auxv contains AT_EXECFN");
    EXPECT_TRUE(
        find_auxv_word(auxv, (size_t) nbytes / 8, AT_EXECFN, &proc_execfn),
        "AT_EXECFN missing");

    TEST("procfs exec: auxv contains AT_RANDOM");
    EXPECT_TRUE(
        find_auxv_word(auxv, (size_t) nbytes / 8, AT_RANDOM, &proc_random),
        "AT_RANDOM missing");

    TEST("procfs exec: auxv contains AT_HWCAP");
    EXPECT_TRUE(
        find_auxv_word(auxv, (size_t) nbytes / 8, AT_HWCAP, &proc_hwcap),
        "AT_HWCAP missing");

    TEST("procfs exec: auxv matches getauxval after exec");
    EXPECT_TRUE(proc_execfn == getauxval(AT_EXECFN) &&
                    proc_random == getauxval(AT_RANDOM) &&
                    proc_hwcap == getauxval(AT_HWCAP),
                "procfs auxv differs from live auxv");

    TEST("procfs exec: /proc/self/environ refreshed");
    {
        ssize_t n = raw_read_file_nul("/proc/self/environ", buf, sizeof(buf));
        bool found = false;
        if (n < 0) {
            FAIL("could not read environ");
        } else {
            for (ssize_t i = 0; i + (ssize_t) sizeof(expect_env) <= n; i++) {
                if (!memcmp(buf + i, expect_env, sizeof(expect_env) - 1)) {
                    found = true;
                    break;
                }
            }
            EXPECT_TRUE(found, "replacement environ missing");
        }
    }
}

int main(int argc, char **argv)
{
    if (argc > 1 && !strcmp(argv[1], "--check")) {
        run_checks();
        SUMMARY("test-procfs-exec");
        return fails > 0 ? 1 : 0;
    }

    char self[512];
    ssize_t n = readlink("/proc/self/exe", self, sizeof(self) - 1);
    if (n < 0) {
        perror("readlink /proc/self/exe");
        return 1;
    }
    self[n] = '\0';

    char *child_argv[] = {self, "--check", NULL};
    char *child_envp[] = {"ELFUSE_PROCFS_AFTER=1", NULL};

    execve(self, child_argv, child_envp);
    perror("execve");
    return 1;
}
