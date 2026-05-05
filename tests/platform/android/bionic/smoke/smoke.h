#pragma once

typedef struct {
    const char* name;
    int passed;
    const char* msg;
    long duration_ns;
} smoke_result_t;

typedef smoke_result_t (*smoke_fn_t)();

typedef struct {
    const char* name;
    smoke_fn_t fn;
} smoke_test_t;

#define SMOKE_PASS(name) (smoke_result_t){name, 1, NULL, 0}
#define SMOKE_FAIL(name, msg) (smoke_result_t){name, 0, msg, 0}
