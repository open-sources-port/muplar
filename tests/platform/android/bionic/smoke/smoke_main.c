#include "smoke.h"
#include <stdio.h>

/* external tests */
smoke_result_t test_thread();
smoke_result_t test_memory();
smoke_result_t test_io();
smoke_result_t test_syscall();

/* runner */
smoke_result_t run_test(smoke_fn_t fn);
void report(smoke_result_t r);

int main() {
    int failed = 0;

    printf("=== Muplar Smoke v3 ===\n");

    smoke_result_t tests[] = {
        run_test(test_thread),
        run_test(test_memory),
        run_test(test_io),
        run_test(test_syscall)
    };

    int n = sizeof(tests) / sizeof(tests[0]);

    for (int i = 0; i < n; i++) {
        report(tests[i]);
        if (!tests[i].passed) failed++;
    }

    printf("\n=====================\n");

    if (failed == 0) {
        printf("SMOKE RESULT: OK\n");
        return 0;
    }

    printf("SMOKE RESULT: FAIL (%d)\n", failed);
    return 1;
}
