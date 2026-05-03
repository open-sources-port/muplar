#include <stdio.h>

#define __fpurge(fp) fpurge(fp)

#define FSETLOCKING_BYCALLER 0

static inline int __fsetlocking(FILE *f, int type) {
    (void)f; (void)type;
    return 0;
}