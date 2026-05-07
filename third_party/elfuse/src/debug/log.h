/* Level-based diagnostic logging
 *
 * Copyright 2026 elfuse contributors
 * SPDX-License-Identifier: Apache-2.0
 *
 * Provides leveled logging (TRACE through FATAL) with timestamps, source
 * file:line, optional ANSI color, and thread-safe output.
 * All output goes to stderr.
 */

#pragma once

#include <stdarg.h>
#include <stdio.h>
#include <time.h>

enum log_level {
    LOG_TRACE = 0,
    LOG_DEBUG,
    LOG_INFO,
    LOG_WARN,
    LOG_ERROR,
    LOG_FATAL,
};

#define log_trace(...) log_impl(LOG_TRACE, __FILE__, __LINE__, __VA_ARGS__)
#define log_debug(...) log_impl(LOG_DEBUG, __FILE__, __LINE__, __VA_ARGS__)
#define log_info(...) log_impl(LOG_INFO, __FILE__, __LINE__, __VA_ARGS__)
#define log_warn(...) log_impl(LOG_WARN, __FILE__, __LINE__, __VA_ARGS__)
#define log_error(...) log_impl(LOG_ERROR, __FILE__, __LINE__, __VA_ARGS__)
#define log_fatal(...) log_impl(LOG_FATAL, __FILE__, __LINE__, __VA_ARGS__)

/* Initialize the logging subsystem. Installs a default pthread-based
 * lock and detects ANSI color support via isatty(STDERR_FILENO).
 * Call once at program startup before any log output.
 */
void log_init(void);

/* Set the minimum log level. Messages below this level are suppressed.
 * Default after log_init() is LOG_WARN.
 */
void log_set_level(int level);

/* Implementation function called by the log_* macros. */
__attribute__((format(printf, 4, 5))) void log_impl(int level,
                                                    const char *file,
                                                    int line,
                                                    const char *fmt,
                                                    ...);
