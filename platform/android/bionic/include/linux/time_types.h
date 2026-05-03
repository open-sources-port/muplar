// time_types_compat.h
#pragma once

#include <time.h>
#include <sys/time.h>

// Linux defines suseconds_t in <bits/types.h>, macOS already has it
typedef suseconds_t suseconds_t;

typedef struct timeval timeval;
typedef struct timespec timespec;
typedef struct itimerspec itimerspec;

/* Map Linux __kernel_timespec to POSIX timespec */
struct __kernel_timespec {
    time_t tv_sec;
    long   tv_nsec;
};

struct __kernel_itimerspec {
    struct __kernel_timespec it_interval;
    struct __kernel_timespec it_value;
};
