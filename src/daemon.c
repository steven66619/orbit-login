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
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>

static volatile int running = 1;
static volatile int child_exited = 0;

static void signal_handler(int sig) {
    switch (sig) {
    case SIGINT:
    case SIGTERM:
        running = 0;
        break;
    case SIGCHLD:
        child_exited = 1;
        break;
    case SIGHUP:
        break;
    default:
        break;
    }
}

static void setup_signals(void) {
    struct sigaction sa;

    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = signal_handler;
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGHUP, &sa, NULL);
    sigaction(SIGCHLD, &sa, NULL);

    signal(SIGPIPE, SIG_IGN);
}

static void daemonize(void) {
    pid_t pid = fork();
    if (pid < 0) exit(1);
    if (pid > 0) exit(0);

    setsid();

    pid = fork();
    if (pid < 0) exit(1);
    if (pid > 0) exit(0);

    chdir("/");

    int fd = open("/dev/null", O_RDWR);
    if (fd >= 0) {
        dup2(fd, STDIN_FILENO);
        dup2(fd, STDOUT_FILENO);
        dup2(fd, STDERR_FILENO);
        if (fd > 2) close(fd);
    }

    umask(022);
}

int main(int argc, char **argv) {
    orbit_config_t cfg;
    display_t disp;
    int srv_fd;
    pid_t greeter_pid = 0;
    int session_active = 0;
    int daemon_mode = 1;

    config_load(NULL, &cfg);

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--foreground") == 0 || strcmp(argv[i], "-f") == 0) {
            daemon_mode = 0;
        } else if (strcmp(argv[i], "--verbose") == 0 || strcmp(argv[i], "-v") == 0) {
            cfg.verbose = 1;
        } else if (strcmp(argv[i], "--config") == 0 && i + 1 < argc) {
            config_load(argv[++i], &cfg);
        } else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            printf("orbitd - Bedrock-aware display manager\n");
            printf("Usage: orbitd [options]\n");
            printf("  -f, --foreground    Run in foreground\n");
            printf("  -v, --verbose       Verbose output\n");
            printf("  -c, --config FILE   Config file path\n");
            printf("  -h, --help          This help\n");
            return 0;
        }
    }

    setup_signals();

    if (daemon_mode) {
        daemonize();
    }

    log_init(cfg.verbose);

    log_msg(0, "orbitd starting...");

    if (getuid() != 0) {
        log_msg(1, "orbitd must be run as root");
        return 1;
    }

    srv_fd = ipc_server_start();
    if (srv_fd < 0) {
        log_msg(1, "Failed to start IPC server");
        return 1;
    }

    stratum_t strata[MAX_STRATA];
    int nstrata = strata_list_enabled(strata, MAX_STRATA);
    log_msg(0, "Detected %d enabled Bedrock strata:", nstrata);
    for (int i = 0; i < nstrata; i++) {
        log_msg(0, "  [%d] %s", i + 1, strata[i].name);
    }

    session_t sessions[MAX_SESSIONS];
    int nsessions = session_discover_all_strata(sessions, MAX_SESSIONS);
    log_msg(0, "Discovered %d desktop sessions across all strata:", nsessions);
    for (int i = 0; i < nsessions; i++) {
        log_msg(0, "  [%d] %s (from %s stratum, type: %s)",
                i + 1, sessions[i].name, sessions[i].stratum,
                sessions[i].type == SESSION_WAYLAND ? "Wayland" : "X11");
    }

    memset(&disp, 0, sizeof(disp));
    disp.vt = cfg.auto_vt ? 0 : cfg.vt_number;
    if (disp.vt <= 0) {
        log_msg(1, "Invalid VT %d from config, falling back to auto-detect", disp.vt);
        disp.vt = 0;
        cfg.auto_vt = 1;
    }

    int start_retries = 0;
    const int max_start_retries = 10;

    while (running) {
        if (disp.xorg_pid <= 0) {
            if (start_retries >= max_start_retries) {
                log_msg(1, "Failed to start Xorg after %d attempts, giving up", max_start_retries);
                break;
            }
            log_msg(0, "Starting Xorg on VT %d...", disp.vt > 0 ? disp.vt : 7);
            if (display_start_xorg(&disp, &cfg) < 0) {
                start_retries++;
                log_msg(1, "Failed to start Xorg (attempt %d/%d), retrying in 2 seconds",
                        start_retries, max_start_retries);
                sleep(2);
                continue;
            }
            log_msg(0, "Display :%s ready on VT %d", disp.display, disp.vt);
        }

        if (greeter_pid <= 0) {
            if (start_retries >= max_start_retries) {
                log_msg(1, "Failed to start greeter after %d attempts, giving up", max_start_retries);
                break;
            }
            log_msg(0, "Starting greeter on display :%s...", disp.display);
            greeter_pid = greeter_start(&disp, &cfg);
            if (greeter_pid < 0) {
                start_retries++;
                log_msg(1, "Failed to start greeter (attempt %d/%d), retrying in 1 second",
                        start_retries, max_start_retries);
                sleep(1);
                continue;
            }
        }

        break;
    }

    if (!running || start_retries >= max_start_retries) {
        if (srv_fd >= 0) {
            unlink(SOCKET_PATH);
            close(srv_fd);
        }
        return 1;
    }

    int xorg_retries = 0;
    int greeter_retries = 0;
    const int max_retries = 5;

    log_msg(0, "orbitd ready. Waiting for connections on %s", SOCKET_PATH);

    while (running) {
        if (child_exited) {
            child_exited = 0;

            if (greeter_pid > 0) {
                int wstatus;
                pid_t wp = waitpid(greeter_pid, &wstatus, WNOHANG);
                if (wp == greeter_pid) {
                    if (WIFEXITED(wstatus) && WEXITSTATUS(wstatus) == 0) {
                        log_msg(0, "Greeter exited normally (session started)");
                        session_active = 1;
                        xorg_retries = 0;
                        greeter_retries = 0;
                    } else {
                        log_msg(0, "Greeter exited (status %d), will restart",
                                WIFEXITED(wstatus) ? WEXITSTATUS(wstatus) : -1);
                    }
                    greeter_pid = 0;
                }
            }

            if (disp.session_pid > 0) {
                int wstatus;
                pid_t wp = waitpid(disp.session_pid, &wstatus, WNOHANG);
                if (wp == disp.session_pid) {
                    log_msg(0, "Session process exited, returning to greeter");
                    disp.session_pid = 0;
                    session_active = 0;
                    xorg_retries = 0;
                    greeter_retries = 0;
                }
            }

            if (disp.xorg_pid > 0) {
                int wstatus;
                pid_t wp = waitpid(disp.xorg_pid, &wstatus, WNOHANG);
                if (wp == disp.xorg_pid) {
                    log_msg(0, "Xorg exited, restarting display");
                    disp.xorg_pid = 0;
                    session_active = 0;
                    xorg_retries = 0;
                }
            }
        }

        if (disp.xorg_pid <= 0) {
            if (xorg_retries >= max_retries) {
                log_msg(1, "Failed to restart Xorg after %d attempts, shutting down", max_retries);
                break;
            }
            display_stop_xorg(&disp);
            log_msg(0, "Restarting Xorg...");
            disp.vt = cfg.auto_vt ? 0 : cfg.vt_number;
            if (disp.vt <= 0) {
                log_msg(1, "Invalid VT %d from config on restart, falling back to auto-detect", disp.vt);
                disp.vt = 0;
            }
            if (display_start_xorg(&disp, &cfg) < 0) {
                xorg_retries++;
                log_msg(1, "Failed to restart Xorg (attempt %d/%d), retrying in 2s",
                        xorg_retries, max_retries);
                sleep(2);
                continue;
            }
            log_msg(0, "Display :%s ready on VT %d", disp.display, disp.vt);
            xorg_retries = 0;
        }

        if (greeter_pid <= 0 && !session_active) {
            if (greeter_retries >= max_retries) {
                log_msg(1, "Failed to start greeter after %d attempts, shutting down", max_retries);
                break;
            }
            log_msg(0, "Starting greeter on display :%s...", disp.display);
            greeter_pid = greeter_start(&disp, &cfg);
            if (greeter_pid < 0) {
                greeter_retries++;
                log_msg(1, "Failed to start greeter (attempt %d/%d), retrying in 1s",
                        greeter_retries, max_retries);
                sleep(1);
                continue;
            }
            greeter_retries = 0;
        }

        fd_set rfds;
        struct timeval tv;

        FD_ZERO(&rfds);
        FD_SET(srv_fd, &rfds);
        int max_fd = srv_fd;

        tv.tv_sec = 1;
        tv.tv_usec = 0;

        int ret = select(max_fd + 1, &rfds, NULL, NULL, &tv);
        if (ret < 0) {
            if (errno == EINTR) continue;
            break;
        }

        if (FD_ISSET(srv_fd, &rfds)) {
            int client_fd = ipc_server_accept(srv_fd);
            if (client_fd >= 0) {
                log_msg(0, "Greeter connected");
                ipc_server_handle_greeter(client_fd, &disp);
            }
        }
    }

    log_msg(0, "Shutting down...");

    if (greeter_pid > 0) {
        kill(greeter_pid, SIGTERM);
        waitpid(greeter_pid, NULL, 0);
    }

    display_stop_xorg(&disp);

    unlink(SOCKET_PATH);
    close(srv_fd);

    log_msg(0, "orbitd stopped");
    return 0;
}
