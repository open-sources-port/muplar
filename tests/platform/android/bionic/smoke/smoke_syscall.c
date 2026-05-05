#include "smoke.h"
#include <stdio.h>
#include <unistd.h>
#include <string.h>

smoke_result_t test_io() {
    const char* path = "/tmp/smoke.txt";

    FILE* f = fopen(path, "w");
    if (!f) return SMOKE_FAIL("io", "write open failed");

    fprintf(f, "ok");
    fclose(f);

    f = fopen(path, "r");
    if (!f) return SMOKE_FAIL("io", "read open failed");

    char buf[8] = {0};
    fread(buf, 1, sizeof(buf), f);
    fclose(f);

    if (strncmp(buf, "ok", 2) != 0)
        return SMOKE_FAIL("io", "content mismatch");

    return SMOKE_PASS("io");
}

smoke_result_t test_syscall() {
    pid_t pid = getpid();
    if (pid <= 0) return SMOKE_FAIL("syscall", "getpid failed");

    return SMOKE_PASS("syscall");
}
