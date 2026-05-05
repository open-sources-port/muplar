#include "smoke.h"
#include <stdio.h>
#include <time.h>

static long now_ns() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000000000L + ts.tv_nsec;
}

smoke_result_t run_test(smoke_fn_t fn) {
    long start = now_ns();
    smoke_result_t r = fn();
    long end = now_ns();

    r.duration_ns = end - start;
    return r;
}

void report(smoke_result_t r) {
    printf("[%s] %s", r.passed ? "PASS" : "FAIL", r.name);

    if (r.msg)
        printf(" -> %s", r.msg);

    printf(" (%ld ms)\n", r.duration_ns / 1000000);
}
