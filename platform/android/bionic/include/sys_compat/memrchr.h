#pragma once
#include <stddef.h>

#if defined(__APPLE__)

/* macOS fallback for memrchr */
static inline void *memrchr(const void *s, int c, size_t n) {
    const unsigned char *p = (const unsigned char *)s + n;
    while (n--) {
        if (*(--p) == (unsigned char)c)
            return (void *)p;
    }
    return NULL;
}

#else
/* On Linux / glibc, use the system-provided memrchr */
#include <string.h>
#endif
