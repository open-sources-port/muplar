/* Level-based diagnostic logging
 *
 * Copyright 2026 elfuse contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#include <pthread.h>
#include <stdbool.h>
#include <unistd.h>

#include "utils.h"

#include "debug/log.h"

#define NUM_LEVELS 6

static int log_level = LOG_WARN;
static bool log_use_color = false;
static pthread_mutex_t log_mutex = PTHREAD_MUTEX_INITIALIZER;

static const char *level_strings[NUM_LEVELS] = {
    "TRACE", "DEBUG", "INFO", "WARN", "ERROR", "FATAL",
};

static const char *level_colors[NUM_LEVELS] = {
    "\x1b[94m", "\x1b[36m", "\x1b[32m", "\x1b[33m", "\x1b[31m", "\x1b[35m",
};

void log_init(void)
{
    log_use_color = isatty(STDERR_FILENO);
}

void log_set_level(int level)
{
    if (level < LOG_TRACE)
        level = LOG_TRACE;
    if (level > LOG_FATAL)
        level = LOG_FATAL;
    log_level = level;
}

void log_impl(int level, const char *file, int line, const char *fmt, ...)
{
    if (level < log_level)
        return;

    int lvl = (RANGE_CHECK(level, 0, NUM_LEVELS)) ? level : LOG_FATAL;

    char tbuf[16];
    time_t t = time(NULL);
    struct tm tm_buf;
    tbuf[strftime(tbuf, sizeof(tbuf), "%H:%M:%S", localtime_r(&t, &tm_buf))] =
        '\0';

    pthread_mutex_lock(&log_mutex);
    if (log_use_color) {
        fprintf(stderr, "%s %s%-5s\x1b[0m \x1b[90m%s:%d:\x1b[0m ", tbuf,
                level_colors[lvl], level_strings[lvl], file, line);
    } else {
        fprintf(stderr, "%s %-5s %s:%d: ", tbuf, level_strings[lvl], file,
                line);
    }
    va_list ap;
    va_start(ap, fmt);
/* Format string originates from caller's literal via log_* macros. */
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wformat-nonliteral"
    vfprintf(stderr, fmt, ap);
#pragma clang diagnostic pop
    va_end(ap);
    fprintf(stderr, "\n");
    fflush(stderr);
    pthread_mutex_unlock(&log_mutex);
}
