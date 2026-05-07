/* exercises all major elfuse subsystems
 *
 * Copyright 2026 elfuse contributors
 * Copyright 2025 Moritz Angermann, zw3rk pte. ltd.
 * SPDX-License-Identifier: Apache-2.0
 *
 * Tests: stdio, file I/O, dir listing, env vars, time, math, memory,
 * string ops, and system info, all in one program.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/utsname.h>

static int failures = 0;

#define CHECK(cond, msg)               \
    do {                               \
        if (!(cond)) {                 \
            printf("FAIL: %s\n", msg); \
            failures++;                \
        }                              \
    } while (0)

int main(int argc, char *argv[])
{
    printf("=== Comprehensive Test ===\n\n");

    /* 1. argv */
    CHECK(argc >= 1, "argc >= 1");
    CHECK(argv[0], "argv[0] not null");

    /* 2. Environment */
    CHECK(getenv("PATH"), "PATH set");

    /* 3. System info */
    struct utsname uts;
    CHECK(uname(&uts) == 0, "uname succeeds");
    CHECK(!strcmp(uts.sysname, "Linux"), "sysname == Linux");
#if defined(__aarch64__)
    CHECK(!strcmp(uts.machine, "aarch64"), "machine == aarch64");
#elif defined(__x86_64__)
    /* x86_64 test binary: uname returns "x86_64" */
    CHECK(!strcmp(uts.machine, "x86_64"), "machine == x86_64");
#endif

    /* 4. PID */
    CHECK(getpid() > 0, "getpid > 0");

    /* 5. Working directory */
    char cwd[256];
    if (getcwd(cwd, sizeof(cwd))) {
        CHECK(1, "getcwd succeeds");
        CHECK(strlen(cwd) > 0, "cwd not empty");
    } else {
        CHECK(0, "getcwd succeeds");
    }

    /* 6. Malloc + realloc + calloc + free */
    int *arr = malloc(100 * sizeof(int));
    CHECK(arr, "malloc(400)");
    if (arr) {
        for (int i = 0; i < 100; i++)
            arr[i] = i;
        int *tmp = realloc(arr, 200 * sizeof(int));
        CHECK(tmp, "realloc(800)");
        if (tmp) {
            arr = tmp;
            CHECK(arr[50] == 50, "realloc preserves data");
        }
    }
    double *d = calloc(10, sizeof(double));
    CHECK(d, "calloc(80)");
    if (d)
        CHECK(d[5] == 0.0, "calloc zeroed");
    free(arr);
    free(d);

    /* 7. String operations */
    char buf[100];
    int len = snprintf(buf, sizeof(buf), "%s %s", "hello", "world");
    CHECK(len > 0 && (size_t) len < sizeof(buf), "snprintf string build");
    CHECK(!strcmp(buf, "hello world"), "string compare");
    CHECK(strlen(buf) == 11, "strlen");
    CHECK(strstr(buf, "world"), "strstr");

    /* 8. Math */
    CHECK(fabs(sqrt(4.0) - 2.0) < 1e-10, "sqrt(4)==2");
    CHECK(fabs(sin(0.0)) < 1e-10, "sin(0)==0");
    CHECK(fabs(cos(0.0) - 1.0) < 1e-10, "cos(0)==1");
    CHECK(fabs(log(M_E) - 1.0) < 1e-10, "log(e)==1");
    CHECK(fabs(pow(2.0, 10.0) - 1024.0) < 1e-10, "2^10==1024");

    /* 9. Time */
    time_t t = time(NULL);
    CHECK(t > 1700000000, "time > 2023");
    struct timespec ts;
    CHECK(clock_gettime(CLOCK_MONOTONIC, &ts) == 0, "clock_gettime");
    CHECK(ts.tv_sec >= 0, "monotonic >= 0");

    /* 10. File I/O roundtrip */
    char path[] = "/tmp/elfuse-comprehensive-test-XXXXXX";
    int tmp_fd = mkstemp(path);
    CHECK(tmp_fd >= 0, "mkstemp");
    FILE *fp = (tmp_fd >= 0) ? fdopen(tmp_fd, "w") : NULL;
    CHECK(fp, "fdopen(w)");
    if (fp) {
        fprintf(fp, "test data: %d\n", 42);
        fclose(fp);
    } else if (tmp_fd >= 0) {
        close(tmp_fd);
    }

    fp = fopen(path, "r");
    CHECK(fp, "fopen(r)");
    int val = 0;
    if (fp) {
        CHECK(fscanf(fp, "test data: %d", &val) == 1, "fscanf");
        CHECK(val == 42, "fscanf value == 42");
        fclose(fp);
    }
    remove(path);

    /* 11. Directory listing */
    DIR *dir = opendir(".");
    CHECK(dir, "opendir(.)");
    int count = 0;
    if (dir) {
        struct dirent *ent;
        while ((ent = readdir(dir)))
            count++;
        closedir(dir);
    }
    CHECK(count > 0, "readdir found entries");

    /* 12. Printf formatting */
    char fmt[100];
    snprintf(fmt, sizeof(fmt), "%d %.2f %s %x", 42, 3.14, "hello", 255);
    CHECK(!strcmp(fmt, "42 3.14 hello ff"), "snprintf formatting");

    /* Summary */
    printf("\n=== Results: %d failures ===\n", failures);
    return failures > 0 ? 1 : 0;
}
