// compat/clock_compat.h
#pragma once

#ifdef __APPLE__

#include <time.h>
#include <sys_compat/muplar_errno.h>
#include <stdbool.h>

/* map Linux clocks to unique macOS values */
#ifndef CLOCK_BOOTTIME
#define CLOCK_BOOTTIME CLOCK_MONOTONIC
#endif

#ifndef CLOCK_REALTIME_ALARM
#define CLOCK_REALTIME_ALARM CLOCK_REALTIME
#endif

#ifndef CLOCK_BOOTTIME_ALARM
#define CLOCK_BOOTTIME_ALARM CLOCK_MONOTONIC
#endif

/* Fake ioctl commands to satisfy compilation */
#define RTC_RD_TIME   0x80247009
#define RTC_SET_TIME  0x4024700a

/* Linux-compatible rtc_time */
struct rtc_time {
    int tm_sec;    /* seconds */
    int tm_min;    /* minutes */
    int tm_hour;   /* hours */
    int tm_mday;   /* day of the month */
    int tm_mon;    /* month */
    int tm_year;   /* year */
    int tm_wday;   /* day of the week */
    int tm_yday;   /* day in the year */
    int tm_isdst;  /* daylight saving time */
};

/* Simulate reading RTC using system time */
static inline int rtc_read_time(int fd, struct rtc_time *rt)
{
    (void) fd;

    if (!rt)
        return -EINVAL;

    time_t now = time(NULL);
    struct tm tm;

    if (gmtime_r(&now, &tm) == NULL)
        return -EIO;

    rt->tm_sec   = tm.tm_sec;
    rt->tm_min   = tm.tm_min;
    rt->tm_hour  = tm.tm_hour;
    rt->tm_mday  = tm.tm_mday;
    rt->tm_mon   = tm.tm_mon;
    rt->tm_year  = tm.tm_year;
    rt->tm_wday  = tm.tm_wday;
    rt->tm_yday  = tm.tm_yday;
    rt->tm_isdst = tm.tm_isdst;

    return 0;
}

/* Setting RTC is not supported on macOS */
static inline int rtc_set_time(int fd, const struct rtc_time *rt)
{
    (void) fd;
    (void) rt;
    return -ENOSYS;
}

/* macOS uses UTC internally */
static inline bool rtc_is_localtime(int fd)
{
    (void) fd;
    return false;
}

#endif // __APPLE__
