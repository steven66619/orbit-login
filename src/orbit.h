#ifndef ORBIT_H
#define ORBIT_H

#include <sys/types.h>
#include <unistd.h>
#include <stdint.h>

#define MAX_STRATUM_NAME 64
#define MAX_SESSION_NAME 128
#define MAX_SESSION_EXEC 512
#define MAX_SESSION_DESKTOP 256
#define MAX_SESSION_PATH 512
#define MAX_STRATA 64
#define MAX_SESSIONS 128
#define MAX_USER_NAME 64
#define MAX_PASS_LEN 256
#define MAX_DISPLAY_NAME 16
#define SOCKET_PATH "/var/run/orbitd.sock"
#define CONF_PATH "/etc/orbit-login.conf"
#define LOG_PATH "/var/log/orbitd.log"
#define XSESSIONS_DIR "/usr/share/xsessions"
#define WAYLAND_SESSIONS_DIR "/usr/share/wayland-sessions"
#define BEDROCK_STRATA_DIR "/bedrock/strata"

typedef enum {
    SESSION_X11,
    SESSION_WAYLAND,
    SESSION_UNKNOWN
} session_type_t;

typedef struct {
    char name[MAX_STRATUM_NAME];
    int  enabled;
    int  hidden;
} stratum_t;

typedef struct {
    char  id[MAX_SESSION_DESKTOP];
    char  name[MAX_SESSION_NAME];
    char  exec[MAX_SESSION_EXEC];
    char  desktop_path[MAX_SESSION_PATH];
    char  stratum[MAX_STRATUM_NAME];
    session_type_t type;
} session_t;

typedef struct {
    char name[MAX_USER_NAME];
    char realname[MAX_USER_NAME];
    uid_t uid;
} user_entry_t;

typedef enum {
    IPC_AUTH_REQ,
    IPC_AUTH_RES,
    IPC_SESSION_LIST,
    IPC_USER_LIST,
    IPC_SESSION_START,
    IPC_SESSION_STATUS,
    IPC_STRATA_LIST,
    IPC_PING,
    IPC_PONG,
    IPC_ERROR
} ipc_msg_type_t;

typedef struct {
    ipc_msg_type_t type;
    uint32_t       seq;
    uint32_t       payload_len;
} ipc_header_t;

#define IPC_MAX_PAYLOAD (64 * 1024)

typedef struct {
    int    vt;
    char   display[MAX_DISPLAY_NAME];
    pid_t  xorg_pid;
    pid_t  session_pid;
    int    active;
} display_t;

typedef struct {
    int   verbose;
    int   auto_vt;
    int   vt_number;
    char  xorg_path[MAX_SESSION_PATH];
    char  xauth_path[MAX_SESSION_PATH];
    char  session_dir[MAX_SESSION_PATH];
    char  wayland_session_dir[MAX_SESSION_PATH];
    int   min_uid;
    int   max_uid;
    char  greeter_user[MAX_USER_NAME];
    int   session_timeout;
} orbit_config_t;

int  strata_list(stratum_t *strata, int max);
int  strata_list_enabled(stratum_t *strata, int max);
int  strata_which_pid(pid_t pid, char *out, size_t outsz);
int  strata_which_bin(const char *bin, char *out, size_t outsz);
int  strata_find_session_file(const char *desktop, char *stratum, size_t stratum_sz);
int  strata_resolve_path(const char *stratum, const char *path, char *out, size_t outsz);

int  session_discover(session_t *sessions, int max, const char *session_dir);
int  session_discover_all_strata(session_t *sessions, int max);
int  session_launch(const session_t *sess, const char *username, const display_t *disp, orbit_config_t *cfg);
int  session_read_desktop(const char *path, session_t *sess);

int  auth_authenticate(const char *user, const char *pass, const display_t *disp, orbit_config_t *cfg);
int  auth_session_open(const char *user, const display_t *disp, orbit_config_t *cfg);
int  auth_session_close(const char *user, const display_t *disp, orbit_config_t *cfg);
int  auth_set_cred(const char *user, const display_t *disp, orbit_config_t *cfg);

int  display_start_xorg(display_t *disp, orbit_config_t *cfg);
int  display_stop_xorg(display_t *disp);
int  display_find_free_vt(int preferred);
int  display_switch_vt(int vt);
int  display_setup_xauth(const char *user, const char *display, orbit_config_t *cfg);

int  ipc_server_start(void);
int  ipc_server_accept(int srv_fd);
int  ipc_server_handle_greeter(int client_fd, display_t *active_disp);
int  ipc_send_response(int fd, ipc_msg_type_t type, uint32_t seq, const void *data, uint32_t len);
int  ipc_recv(int fd, ipc_header_t *hdr, void *payload, uint32_t max_payload);
int  ipc_send(int fd, ipc_msg_type_t type, const void *data, uint32_t len);

int  greeter_start(display_t *disp, orbit_config_t *cfg);
int  greeter_wait(pid_t greeter_pid, int timeout_sec);

int  config_load(const char *path, orbit_config_t *cfg);
void config_defaults(orbit_config_t *cfg);

void log_msg(int level, const char *fmt, ...);
void log_init(int verbose);

#endif
