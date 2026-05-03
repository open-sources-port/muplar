
/* SPDX-License-Identifier: LGPL-2.1-or-later */
#pragma once

/* personality values - Linux compatibility only */
#define PER_LINUX       0x0000
#define PER_LINUX_32BIT 0x0001
#define PER_SVR4        0x0002
#define PER_SCOSVR4     0x0003
#define PER_WYSEV386    0x0004
#define PER_ISCR4       0x0005

/* Stubs for macOS */
#define PER_LINUX32    1
#ifndef PERSONALITY_INVALID
/* personality(7) documents that 0xffffffffUL is used for querying the
 * current personality, hence let's use that here as error
 * indicator. */
#define PERSONALITY_INVALID 0xffffffffLU
#endif

static inline int personality(unsigned long persona) {
    /* macOS does not support Linux personalities, so noop */
    (void) persona;
    return 0;
}