#pragma once

#ifdef __APPLE__
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

// Use cpu_set_t as an opaque struct for compatibility
typedef struct {
    unsigned ncpus;   // macOS compatibility
    unsigned n;       // alias for Linux code
    uint64_t *bits;
} cpu_set_t;

// Allocate memory for cpu_set_t
#define CPU_ALLOC(ncpus) ({                \
    cpu_set_t *_c = malloc(sizeof(cpu_set_t)); \
    if (_c) {                             \
        _c->ncpus = (ncpus);              \
        _c->bits = calloc((ncpus + 63)/64, sizeof(uint64_t)); \
    }                                     \
    _c;                                    \
})

// Free memory
#define CPU_FREE(c) do {               \
    if (c) {                           \
        free((c)->bits);               \
        free(c);                       \
    }                                   \
} while(0)

// Return allocation size
#define CPU_ALLOC_SIZE(ncpus) (sizeof(cpu_set_t) + sizeof(uint64_t)*((ncpus+63)/64))

// Check CPU bit
#define CPU_ISSET_S(cpu, size, c) \
    (((cpu) < (c)->ncpus) ? (((c)->bits[(cpu)/64] >> ((cpu)%64)) & 1) : 0)

// Set a CPU bit
#define CPU_SET_S(cpu, size, c) \
    do { \
        if ((cpu) < (c)->ncpus) \
            (c)->bits[(cpu)/64] |= (1ULL << ((cpu)%64)); \
    } while(0)

// Clear a CPU bit
#define CPU_CLR_S(cpu, size, c) \
    do { \
        if ((cpu) < (c)->ncpus) \
            (c)->bits[(cpu)/64] &= ~(1ULL << ((cpu)%64)); \
    } while(0)

#define CPU_COUNT_S(size, c) ({               \
    unsigned _count = 0;                     \
    for (unsigned _i = 0; _i < (c)->ncpus; _i++) \
        _count += CPU_ISSET_S(_i, size, c); \
    _count;                                  \
})

static inline int sched_getaffinity(int pid, size_t size, cpu_set_t *set) {
    (void)pid; (void)size; (void)set;
    return -1; // failure
}

#endif
