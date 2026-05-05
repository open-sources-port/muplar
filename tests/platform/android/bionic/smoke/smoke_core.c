#include "smoke.h"
#include <stdlib.h>
#include <pthread.h>

/* THREAD TEST */

static void* thread_func(void* arg) {
    (void)arg;
    return NULL;
}

smoke_result_t test_thread() {
    pthread_t t;

    if (pthread_create(&t, NULL, thread_func, NULL) != 0) {
        return SMOKE_FAIL("thread", "pthread_create failed");
    }

    pthread_join(t, NULL);
    return SMOKE_PASS("thread");
}

/* MEMORY TEST */

smoke_result_t test_memory() {
    int* p = malloc(sizeof(int));
    if (!p) return SMOKE_FAIL("memory", "malloc failed");

    *p = 42;
    if (*p != 42) {
        free(p);
        return SMOKE_FAIL("memory", "corruption");
    }

    free(p);
    return SMOKE_PASS("memory");
}
