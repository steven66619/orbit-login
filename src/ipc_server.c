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
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/stat.h>
#include <errno.h>

int ipc_server_start(void) {
    struct sockaddr_un addr;
    int fd;

    unlink(SOCKET_PATH);

    fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        log_msg(1, "Cannot create socket: %s", strerror(errno));
        return -1;
    }

    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", SOCKET_PATH);

    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        log_msg(1, "Cannot bind socket %s: %s", SOCKET_PATH, strerror(errno));
        close(fd);
        return -1;
    }

    chmod(SOCKET_PATH, 0600);

    if (listen(fd, 16) < 0) {
        log_msg(1, "Cannot listen on socket: %s", strerror(errno));
        close(fd);
        return -1;
    }

    log_msg(0, "IPC server listening on %s", SOCKET_PATH);
    return fd;
}

int ipc_server_accept(int srv_fd) {
    struct sockaddr_un client;
    socklen_t len = sizeof(client);

    int fd = accept(srv_fd, (struct sockaddr *)&client, &len);
    if (fd < 0) {
        log_msg(1, "Accept failed: %s", strerror(errno));
        return -1;
    }

    return fd;
}

int ipc_send(int fd, ipc_msg_type_t type, const void *data, uint32_t len) {
    return ipc_send_response(fd, type, 0, data, len);
}

int ipc_send_response(int fd, ipc_msg_type_t type, uint32_t seq, const void *data, uint32_t len) {
    ipc_header_t hdr;

    hdr.type = type;
    hdr.seq = seq;
    hdr.payload_len = len;

    if (write(fd, &hdr, sizeof(hdr)) != sizeof(hdr)) return -1;

    if (len > 0 && data) {
        if (write(fd, data, len) != (ssize_t)len) return -1;
    }

    return 0;
}

int ipc_recv(int fd, ipc_header_t *hdr, void *payload, uint32_t max_payload) {
    memset(hdr, 0, sizeof(*hdr));

    if (read(fd, hdr, sizeof(*hdr)) != sizeof(*hdr)) return -1;

    if (hdr->payload_len > 0) {
        if (hdr->payload_len > max_payload) return -1;

        ssize_t n = read(fd, payload, hdr->payload_len);
        if (n < 0 || (uint32_t)n != hdr->payload_len) return -1;
    }

    return 0;
}

static int handle_auth_request(int client_fd, ipc_header_t *hdr, void *payload, orbit_config_t *cfg) {
    char *user = payload;
    char *pass = payload;

    if (hdr->payload_len < 2) {
        ipc_send_response(client_fd, IPC_AUTH_RES, hdr->seq, "0", 1);
        return 0;
    }

    size_t user_len = strnlen(user, hdr->payload_len);
    if (user_len >= hdr->payload_len) {
        ipc_send_response(client_fd, IPC_AUTH_RES, hdr->seq, "0", 1);
        return 0;
    }

    pass = user + user_len + 1;
    size_t pass_len = strnlen(pass, hdr->payload_len - user_len - 1);

    if (user_len == 0 || pass_len == 0) {
        ipc_send_response(client_fd, IPC_AUTH_RES, hdr->seq, "0", 1);
        return 0;
    }

    display_t disp = {0};
    snprintf(disp.display, sizeof(disp.display), "%d", 0);

    int result = auth_authenticate(user, pass, &disp, cfg);

    memset(pass, 0, pass_len);

    if (result == 0) {
        ipc_send_response(client_fd, IPC_AUTH_RES, hdr->seq, "1", 1);
    } else {
        ipc_send_response(client_fd, IPC_AUTH_RES, hdr->seq, "0", 1);
    }

    return 0;
}

static int handle_session_list(int client_fd, ipc_header_t *hdr, void *payload, orbit_config_t *cfg) {
    session_t sessions[MAX_SESSIONS];
    int n;

    (void)payload;
    (void)cfg;

    n = session_discover_all_strata(sessions, MAX_SESSIONS);

    uint32_t total_size = n * sizeof(session_t);
    uint32_t max_send = IPC_MAX_PAYLOAD;
    if (total_size > max_send) {
        n = max_send / sizeof(session_t);
        total_size = n * sizeof(session_t);
    }

    ipc_send_response(client_fd, IPC_SESSION_LIST, hdr->seq, sessions, total_size);
    return 0;
}

static int handle_user_list(int client_fd, ipc_header_t *hdr, void *payload, orbit_config_t *cfg) {
    user_entry_t users[256];
    int count = 0;

    (void)payload;

    FILE *fp = fopen("/etc/passwd", "r");
    if (fp) {
        char line[512];
        while (fgets(line, sizeof(line), fp) && count < 256) {
            char name[64], rest[512];
            int uid_int;
            if (sscanf(line, "%63[^:]:%*[^:]:%d:%*[^:]:%511[^:]:%*s",
                       name, &uid_int, rest) >= 3) {
                if (uid_int >= cfg->min_uid && uid_int <= cfg->max_uid) {
                    snprintf(users[count].name, sizeof(users[count].name), "%s", name);
                    snprintf(users[count].realname, sizeof(users[count].realname), "%s", rest);
                    users[count].uid = uid_int;
                    count++;
                }
            }
        }
        fclose(fp);
    }

    uint32_t total_size = count * sizeof(user_entry_t);
    uint32_t max_send = IPC_MAX_PAYLOAD;
    if (total_size > max_send) {
        count = max_send / sizeof(user_entry_t);
        total_size = count * sizeof(user_entry_t);
    }

    ipc_send_response(client_fd, IPC_USER_LIST, hdr->seq, users, total_size);
    return 0;
}

static int handle_session_start(int client_fd, ipc_header_t *hdr, void *payload, orbit_config_t *cfg, display_t *active_disp) {
    pid_t session_pid;

    if (hdr->payload_len < sizeof(session_t)) {
        ipc_send_response(client_fd, IPC_ERROR, hdr->seq, "invalid payload", 15);
        return 0;
    }

    session_t sess;
    memcpy(&sess, payload, sizeof(session_t));

    display_t disp;
    memcpy(&disp, active_disp, sizeof(disp));

    char *username = (char *)payload + sizeof(session_t);
    size_t userlen = strnlen(username, hdr->payload_len - sizeof(session_t));

    if (userlen == 0) {
        ipc_send_response(client_fd, IPC_ERROR, hdr->seq, "no username", 11);
        return 0;
    }

    if (auth_session_open(username, &disp, cfg) < 0) {
        log_msg(1, "pam_open_session failed for '%s'", username);
    }

    if (auth_set_cred(username, &disp, cfg) < 0) {
        log_msg(1, "pam_setcred failed for '%s'", username);
    }

    session_pid = session_launch(&sess, username, &disp, cfg);

    active_disp->session_pid = session_pid;

    if (session_pid > 0) {
        char stratum_info[MAX_STRATUM_NAME + 32];
        snprintf(stratum_info, sizeof(stratum_info), "%s", sess.stratum);

        sleep(1);

        char detected_stratum[MAX_STRATUM_NAME];
        if (strata_which_pid(session_pid, detected_stratum, sizeof(detected_stratum)) == 0) {
            log_msg(0, "Session running from stratum: %s", detected_stratum);
        }

        ipc_send(client_fd, IPC_SESSION_STATUS, stratum_info, strlen(stratum_info) + 1);
    } else {
        ipc_send_response(client_fd, IPC_ERROR, hdr->seq, "launch failed", 13);
    }

    return 0;
}

int ipc_server_handle_greeter(int client_fd, display_t *active_disp) {
    uint8_t payload[IPC_MAX_PAYLOAD];
    ipc_header_t hdr;
    orbit_config_t cfg;

    config_load(NULL, &cfg);

    while (1) {
        if (ipc_recv(client_fd, &hdr, payload, sizeof(payload)) < 0) {
            break;
        }

        switch (hdr.type) {
        case IPC_AUTH_REQ:
            handle_auth_request(client_fd, &hdr, payload, &cfg);
            break;
        case IPC_SESSION_LIST:
            handle_session_list(client_fd, &hdr, payload, &cfg);
            break;
        case IPC_USER_LIST:
            handle_user_list(client_fd, &hdr, payload, &cfg);
            break;
        case IPC_SESSION_START:
            handle_session_start(client_fd, &hdr, payload, &cfg, active_disp);
            break;
        case IPC_STRATA_LIST:
            {
                stratum_t strata[MAX_STRATA];
                int n = strata_list_enabled(strata, MAX_STRATA);
                uint32_t size = n * sizeof(stratum_t);
                ipc_send_response(client_fd, IPC_STRATA_LIST, hdr.seq, strata, size);
            }
            break;
        case IPC_PING:
            ipc_send(client_fd, IPC_PONG, "pong", 5);
            break;
        default:
            ipc_send_response(client_fd, IPC_ERROR, hdr.seq, "unknown msg", 11);
            break;
        }
    }

    close(client_fd);
    return 0;
}
