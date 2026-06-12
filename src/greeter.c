/* SPDX-License-Identifier: GPL-3.0-only
 *
 * orbit-login - display manager for Bedrock Linux
 * Copyright (C) 2025  Steven Ende
 */

#include "orbit.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>

int greeter_start(display_t *disp, orbit_config_t *cfg) {
    pid_t pid;
    char display_str[32];
    char xauth_path[MAX_SESSION_PATH];

    snprintf(display_str, sizeof(display_str), ":%s", disp->display);
    snprintf(xauth_path, sizeof(xauth_path), "%s-%s", cfg->xauth_path, disp->display);

    pid = fork();
    if (pid < 0) return -1;

    if (pid > 0) return pid;

    setsid();

    setenv("DISPLAY", display_str, 1);
    setenv("XAUTHORITY", xauth_path, 1);
    setenv("QT_QPA_PLATFORM", "xcb", 1);

    sleep(1);

    execlp("orbit-greeter", "orbit-greeter", (char *)NULL);

    char greeter_path[MAX_SESSION_PATH];
    snprintf(greeter_path, sizeof(greeter_path), "%s/orbit-greeter",
             "/usr/local/libexec");
    execlp(greeter_path, greeter_path, (char *)NULL);

    execlp("./orbit-greeter", "./orbit-greeter", (char *)NULL);

    _exit(127);
}

int greeter_wait(pid_t pid, int timeout_sec) {
    int status;
    pid_t result;

    if (timeout_sec > 0) {
        int waited = 0;
        while (waited < timeout_sec) {
            result = waitpid(pid, &status, WNOHANG);
            if (result == pid) {
                return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
            }
            sleep(1);
            waited++;
        }
        kill(pid, SIGTERM);
        waitpid(pid, &status, 0);
        return -1;
    } else {
        result = waitpid(pid, &status, 0);
        if (result == pid) {
            return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
        }
    }

    return -1;
}
