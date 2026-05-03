// compat_malloc.h
#pragma once

#include <stdlib.h>
#include <sys_compat/muplar_errno.h>
#include <sys_compat/muplar_limits.h>
#include <malloc/malloc.h>
#include <string.h>

#ifndef HAVE_EXPLICIT_BZERO
#define HAVE_EXPLICIT_BZERO 1
#endif
static void explicit_bzero(void *p, size_t n) {
    volatile unsigned char *vp = (volatile unsigned char *)p;
    while (n--) *vp++ = 0;
}

static inline void *mempcpy(void *dst, const void *src, size_t n) {
    memcpy(dst, src, n);
    return (char *)dst + n;
}

#define malloc_usable_size(x) malloc_size(x)

// If code expects memalign, redirect it to posix_memalign
static inline void* memalign(size_t alignment, size_t size) {
    void* ptr = NULL;
    if (posix_memalign(&ptr, alignment, size) != 0) {
        return NULL;
    }
    return ptr;
}

// malloc_trim doesn’t exist on macOS → stub it out
static inline int malloc_trim(size_t __attribute__((unused)) pad) {
    return 0; // no-op
}

// mallinfo is Linux-only; stub if needed
struct mallinfo {
    int arena;    // non-mmapped space allocated (bytes)
    int ordblks;  // number of free chunks
    int smblks;
    int hblks;
    int hblkhd;
    int usmblks;
    int fsmblks;
    int uordblks; // total allocated space
    int fordblks; // total free space
    int keepcost;
};

static inline struct mallinfo mallinfo(void) {
    struct mallinfo mi = {0};
    return mi; // return empty stats
}

#ifndef reallocarray
static inline void *reallocarray(void *ptr, size_t nmemb, size_t size) {
    if (nmemb == 0 || size == 0) {
        // realloc allows zero size, just delegate
        return realloc(ptr, nmemb * size);
    }

    // Check for overflow
    if (nmemb > SIZE_MAX / size) {
        errno = ENOMEM;
        return NULL;
    }

    return realloc(ptr, nmemb * size);
}
#endif
