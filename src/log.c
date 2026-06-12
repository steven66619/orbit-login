/* SPDX-License-Identifier: GPL-3.0-only
 *
 * orbit-login - display manager for Bedrock Linux
 * Copyright (C) 2025  Steven Ende
 */

#include "orbit.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <time.h>
#include <syslog.h>
#include <sys/stat.h>

static int log_verbose = 1;
static FILE *log_file = NULL;

void log_init(int verbose) {
    log_verbose = verbose;
    openlog("orbitd", LOG_CONS | LOG_PID, LOG_AUTH);

    log_file = fopen(LOG_PATH, "a");
    if (log_file) {
        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        fprintf(log_file, "\n--- orbitd start [uptime %lu.%09lus] ---\n",
                (unsigned long)ts.tv_sec, (unsigned long)ts.tv_nsec);
        fflush(log_file);
    }
}

void log_msg(int level, const char *fmt, ...) {
    va_list ap;
    time_t now;
    struct tm *tm;
    char timestamp[64];

    time(&now);
    tm = localtime(&now);
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", tm);

    va_start(ap, fmt);

    if (log_verbose) {
        fprintf(stdout, "[%s] ", timestamp);
        vfprintf(stdout, fmt, ap);
        fprintf(stdout, "\n");
        fflush(stdout);
    }

    va_end(ap);

    if (log_file) {
        va_start(ap, fmt);
        fprintf(log_file, "[%s] ", timestamp);
        vfprintf(log_file, fmt, ap);
        fprintf(log_file, "\n");
        fflush(log_file);
        va_end(ap);
    }

    va_start(ap, fmt);
    vsyslog(level ? LOG_ERR : LOG_INFO, fmt, ap);
    va_end(ap);
}
