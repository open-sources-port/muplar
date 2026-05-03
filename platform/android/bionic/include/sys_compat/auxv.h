#pragma once

#ifndef __linux__

// dummy flags to keep code building
#define AT_NULL   0
#define AT_PAGESZ 6
#define AT_HWCAP  16
#define AT_PLATFORM 15

static inline unsigned long getauxval(unsigned long type) {
    // No auxv on macOS, return 0 so callers can check
    return 0;
}

#endif /* __linux__ */
