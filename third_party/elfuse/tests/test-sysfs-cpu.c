/* Test /sys/devices/system/cpu emulation
 *
 * Copyright 2026 elfuse contributors
 * SPDX-License-Identifier: Apache-2.0
 *
 * Tests: /sys/devices/system/cpu/{online,possible,present} read,
 *        opendir + readdir on /sys/devices/system/cpu, stat on cpuN
 *        directories, and ENOENT on the cache/topology subtrees that the
 *        stub deliberately leaves empty, plus access()/faccessat() mode
 *        checks for the read-only synthetic tree.
 *
 * Syscalls exercised: openat(56), read(63), close(57), getdents64(61),
 *                     newfstatat(79).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <dirent.h>
#include <sys/stat.h>

#include "test-harness.h"
#include "test-util.h"

/* Parse a "0\n" or "0-N\n" cpumask range file into the highest set CPU
 * index. Returns -1 on malformed input.
 */
static int parse_cpurange(const char *s, ssize_t len)
{
    if (len <= 0)
        return -1;
    /* Skip leading whitespace just in case */
    int i = 0;
    while (i < len && (s[i] == ' ' || s[i] == '\t'))
        i++;
    if (i >= len || s[i] < '0' || s[i] > '9')
        return -1;
    int low = 0;
    while (i < len && s[i] >= '0' && s[i] <= '9')
        low = low * 10 + (s[i++] - '0');

    if (i < len && s[i] == '-') {
        i++;
        if (i >= len || s[i] < '0' || s[i] > '9')
            return -1;
        int high = 0;
        while (i < len && s[i] >= '0' && s[i] <= '9')
            high = high * 10 + (s[i++] - '0');
        return high;
    }
    return low;
}

/* Read a cpumask range file (online/possible/present) and return the
 * highest CPU index it advertises. -1 on read or parse failure.
 */
static int read_cpurange(const char *path)
{
    char buf[64];
    ssize_t n = read_file_nul(path, buf, sizeof(buf));
    if (n <= 0)
        return -1;
    return parse_cpurange(buf, n);
}

int main(void)
{
    int passes = 0, fails = 0;

    printf("test-sysfs-cpu: /sys/devices/system/cpu emulation\n");

    TEST("read /sys/devices/system/cpu/online");
    int max_cpu = read_cpurange("/sys/devices/system/cpu/online");
    EXPECT_TRUE(max_cpu >= 0, "online read or parse failed");

    TEST("possible matches online");
    EXPECT_EQ(read_cpurange("/sys/devices/system/cpu/possible"), max_cpu,
              "possible disagrees with online");

    TEST("present matches online");
    EXPECT_EQ(read_cpurange("/sys/devices/system/cpu/present"), max_cpu,
              "present disagrees with online");

    TEST("readdir lists cpu0");
    {
        DIR *dir = opendir("/sys/devices/system/cpu");
        if (dir) {
            int found_cpu0 = 0;
            struct dirent *de;
            while ((de = readdir(dir))) {
                if (!strcmp(de->d_name, "cpu0")) {
                    found_cpu0 = 1;
                    break;
                }
            }
            closedir(dir);
            EXPECT_TRUE(found_cpu0, "cpu0 entry not found");
        } else
            FAIL("opendir failed");
    }

    TEST("cpu0 is a directory");
    {
        struct stat st;
        if (stat("/sys/devices/system/cpu/cpu0", &st) == 0)
            EXPECT_TRUE(S_ISDIR(st.st_mode), "cpu0 is not a directory");
        else
            FAIL("stat failed");
    }

    TEST("ENOENT on missing topology subtree");
    {
        errno = 0;
        int fd =
            open("/sys/devices/system/cpu/cpu0/topology/core_id", O_RDONLY);
        if (fd < 0 && errno == ENOENT) {
            PASS();
        } else {
            if (fd >= 0)
                close(fd);
            FAIL("expected ENOENT for empty subtree");
        }
    }

    TEST("opendir on /sys/devices/system/cpu enumerates ncpu cpuN dirs");
    {
        DIR *dir = opendir("/sys/devices/system/cpu");
        if (dir) {
            int ncpu_dirs = 0;
            struct dirent *de;
            while ((de = readdir(dir))) {
                if (!strncmp(de->d_name, "cpu", 3) && de->d_name[3] >= '0' &&
                    de->d_name[3] <= '9') {
                    ncpu_dirs++;
                }
            }
            closedir(dir);
            EXPECT_EQ(ncpu_dirs, max_cpu + 1, "cpuN dir count != online+1");
        } else
            FAIL("opendir failed");
    }

    /* The stub is read-only: O_WRONLY / O_RDWR / O_CREAT / O_TRUNC must
     * fail with EACCES so a guest cannot mutate the synthetic tree (and
     * cannot pivot a creation into the host scratch dir).
     */
    TEST("EACCES on O_WRONLY of online");
    {
        errno = 0;
        int fd = open("/sys/devices/system/cpu/online", O_WRONLY);
        if (fd >= 0)
            close(fd);
        EXPECT_TRUE(fd < 0 && errno == EACCES, "writable open accepted");
    }

    TEST("EACCES on O_WRONLY of sysfs cpu root");
    {
        errno = 0;
        int fd = open("/sys/devices/system/cpu", O_WRONLY);
        if (fd >= 0)
            close(fd);
        EXPECT_TRUE(fd < 0 && errno == EACCES, "writable root open accepted");
    }

    TEST("EACCES on O_CREAT of new entry");
    {
        errno = 0;
        int fd =
            open("/sys/devices/system/cpu/intruder", O_WRONLY | O_CREAT, 0644);
        if (fd >= 0) {
            close(fd);
            unlink("/sys/devices/system/cpu/intruder");
        }
        EXPECT_TRUE(fd < 0 && errno == EACCES, "O_CREAT accepted");
    }

    TEST("access reports online readable but not writable or executable");
    {
        EXPECT_TRUE(access("/sys/devices/system/cpu/online", F_OK) == 0,
                    "F_OK failed");
        EXPECT_TRUE(access("/sys/devices/system/cpu/online", R_OK) == 0,
                    "R_OK failed");
        errno = 0;
        EXPECT_TRUE(access("/sys/devices/system/cpu/online", W_OK) < 0 &&
                        errno == EACCES,
                    "W_OK unexpectedly succeeded");
        errno = 0;
        EXPECT_TRUE(access("/sys/devices/system/cpu/online", X_OK) < 0 &&
                        errno == EACCES,
                    "X_OK unexpectedly succeeded");
    }

    TEST("access reports cpu root searchable but not writable");
    {
        EXPECT_TRUE(access("/sys/devices/system/cpu", R_OK) == 0,
                    "cpu root R_OK failed");
        EXPECT_TRUE(access("/sys/devices/system/cpu", X_OK) == 0,
                    "cpu root X_OK failed");
        errno = 0;
        EXPECT_TRUE(
            access("/sys/devices/system/cpu", W_OK) < 0 && errno == EACCES,
            "cpu root W_OK unexpectedly succeeded");
    }

    /* '..' in the suffix must not let the open/stat fall through onto an
     * arbitrary host path. The stub keeps the tree closed against
     * traversal regardless of where the scratch dir happens to live.
     */
    TEST("EACCES on dotdot traversal in open");
    {
        errno = 0;
        int fd = open("/sys/devices/system/cpu/../../etc/hostname", O_RDONLY);
        if (fd >= 0)
            close(fd);
        EXPECT_TRUE(fd < 0 && errno == EACCES, "dotdot traversal accepted");
    }

    TEST("EACCES on dotdot traversal in stat");
    {
        struct stat st;
        errno = 0;
        int rc = stat("/sys/devices/system/cpu/../../etc/hostname", &st);
        EXPECT_TRUE(rc < 0 && errno == EACCES,
                    "dotdot traversal accepted in stat");
    }

    SUMMARY("test-sysfs-cpu");
    return fails > 0 ? 1 : 0;
}
