#include "orbit.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <pwd.h>
#include <grp.h>
#include <errno.h>

static int parse_desktop_line(const char *line, const char *key, char *out, size_t outsz) {
    size_t klen = strlen(key);
    if (strncmp(line, key, klen) != 0) return -1;
    if (line[klen] != '=') return -1;

    const char *val = line + klen + 1;

    while (*val == ' ' || *val == '\t') val++;

    size_t vlen = strlen(val);
    while (vlen > 0 && (val[vlen - 1] == '\n' || val[vlen - 1] == '\r')) vlen--;

    if (vlen >= outsz) vlen = outsz - 1;
    memcpy(out, val, vlen);
    out[vlen] = '\0';

    return 0;
}

int session_read_desktop(const char *path, session_t *sess) {
    FILE *fp;
    char line[1024];
    int in_desktop_entry = 0;
    int has_name = 0, has_exec = 0;

    memset(sess, 0, sizeof(session_t));

    fp = fopen(path, "r");
    if (!fp) return -1;

    const char *fname = strrchr(path, '/');
    fname = fname ? fname + 1 : path;
    snprintf(sess->id, sizeof(sess->id), "%s", fname);
    snprintf(sess->desktop_path, sizeof(sess->desktop_path), "%s", path);

    char *dot = strrchr(sess->id, '.');
    if (dot) *dot = '\0';

    while (fgets(line, sizeof(line), fp)) {
        size_t len = strlen(line);
        if (len > 0 && line[len - 1] == '\n') line[len - 1] = '\0';

        if (line[0] == '[') {
            in_desktop_entry = (strcmp(line, "[Desktop Entry]") == 0);
            continue;
        }

        if (!in_desktop_entry) continue;
        if (line[0] == '#' || line[0] == '\0') continue;

        if (parse_desktop_line(line, "Name", sess->name, sizeof(sess->name)) == 0)
            has_name = 1;
        else if (parse_desktop_line(line, "Exec", sess->exec, sizeof(sess->exec)) == 0)
            has_exec = 1;
        else if (parse_desktop_line(line, "Type", line, sizeof(line)) == 0) {
        }
    }

    fclose(fp);

    if (!has_name) {
        snprintf(sess->name, sizeof(sess->name), "%s", sess->id);
    }

    if (!has_exec) {
        snprintf(sess->exec, sizeof(sess->exec), "%s", sess->id);
    }

    char *type_check = strstr(sess->desktop_path, "/wayland-sessions/");
    if (type_check) {
        sess->type = SESSION_WAYLAND;
    } else {
        sess->type = SESSION_X11;
    }

    return 0;
}

static int is_desktop_file(const char *name) {
    size_t len = strlen(name);
    return len > 8 && strcmp(name + len - 8, ".desktop") == 0;
}

int session_discover(session_t *sessions, int max, const char *dir) {
    DIR *d;
    struct dirent *de;
    int count = 0;
    char path[MAX_SESSION_PATH];

    d = opendir(dir);
    if (!d) return 0;

    while ((de = readdir(d)) != NULL && count < max) {
        if (!is_desktop_file(de->d_name)) continue;

        snprintf(path, sizeof(path), "%s/%s", dir, de->d_name);

        if (session_read_desktop(path, &sessions[count]) == 0) {
            char stratum[MAX_STRATUM_NAME];
            if (strata_find_session_file(de->d_name, stratum, sizeof(stratum)) == 0) {
                snprintf(sessions[count].stratum, sizeof(sessions[count].stratum), "%s", stratum);
            } else {
                snprintf(sessions[count].stratum, sizeof(sessions[count].stratum), "native");
            }
            count++;
        }
    }

    closedir(d);
    return count;
}

static void strip_desktop_ext(const char *name, char *out, size_t outsz) {
    size_t len = strlen(name);
    const char *dot = len > 8 ? strrchr(name, '.') : NULL;

    if (dot && strcmp(dot, ".desktop") == 0) {
        size_t base_len = dot - name;
        if (base_len >= outsz) base_len = outsz - 1;
        memcpy(out, name, base_len);
        out[base_len] = '\0';
    } else {
        snprintf(out, outsz, "%s", name);
    }
}

int session_discover_all_strata(session_t *sessions, int max) {
    stratum_t strata[MAX_STRATA];
    int n = strata_list_enabled(strata, MAX_STRATA);
    int count = 0;
    char entry_id[MAX_SESSION_DESKTOP];

    for (int i = 0; i < n && count < max; i++) {
        char dir[MAX_SESSION_PATH];
        snprintf(dir, sizeof(dir),
                 BEDROCK_STRATA_DIR "/%s/usr/share/xsessions",
                 strata[i].name);

        DIR *d = opendir(dir);
        if (!d) continue;

        struct dirent *de;
        while ((de = readdir(d)) != NULL && count < max) {
            if (!is_desktop_file(de->d_name)) continue;

            strip_desktop_ext(de->d_name, entry_id, sizeof(entry_id));

            int already = 0;
            for (int j = 0; j < count; j++) {
                if (strcmp(sessions[j].id, entry_id) == 0) {
                    already = 1;
                    break;
                }
            }
            if (already) continue;

            char path[MAX_SESSION_PATH];
            snprintf(path, sizeof(path), "%s/%s", dir, de->d_name);

            if (session_read_desktop(path, &sessions[count]) == 0) {
                snprintf(sessions[count].stratum, sizeof(sessions[count].stratum), "%s", strata[i].name);
                count++;
            }
        }
        closedir(d);
    }

    char native_dir[MAX_SESSION_PATH];
    snprintf(native_dir, sizeof(native_dir), "%s", XSESSIONS_DIR);

    DIR *d = opendir(native_dir);
    if (d) {
        struct dirent *de;
        while ((de = readdir(d)) != NULL && count < max) {
            if (!is_desktop_file(de->d_name)) continue;

            strip_desktop_ext(de->d_name, entry_id, sizeof(entry_id));

            int already = 0;
            for (int j = 0; j < count; j++) {
                if (strcmp(sessions[j].id, entry_id) == 0) {
                    already = 1;
                    break;
                }
            }
            if (already) continue;

            char path[MAX_SESSION_PATH];
            snprintf(path, sizeof(path), "%s/%s", native_dir, de->d_name);

            if (session_read_desktop(path, &sessions[count]) == 0) {
                snprintf(sessions[count].stratum, sizeof(sessions[count].stratum), "native");
                count++;
            }
        }
        closedir(d);
    }

    return count;
}

int session_launch(const session_t *sess, const char *username, const display_t *disp, orbit_config_t *cfg) {
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
    setenv("HOME", "/home", 1);

    char home_dir[MAX_SESSION_PATH];
    snprintf(home_dir, sizeof(home_dir), "/home/%s", username);
    setenv("HOME", home_dir, 1);

    struct passwd *pw = getpwnam(username);
    if (pw) {
        setenv("HOME", pw->pw_dir, 1);
        setenv("USER", pw->pw_name, 1);
        setenv("LOGNAME", pw->pw_name, 1);
        setenv("SHELL", pw->pw_shell, 1);
        chdir(pw->pw_dir);

        char user_xauth[MAX_SESSION_PATH];
        snprintf(user_xauth, sizeof(user_xauth), "%s/.Xauthority", pw->pw_dir);
        char xa_cmd[MAX_SESSION_PATH * 2 + 100];
        snprintf(xa_cmd, sizeof(xa_cmd),
                 "xauth -f '%s' extract - ':%s' 2>/dev/null | xauth -f '%s' merge - 2>/dev/null",
                 xauth_path, disp->display, user_xauth);
        system(xa_cmd);
        chown(user_xauth, pw->pw_uid, pw->pw_gid);
        setenv("XAUTHORITY", user_xauth, 1);
    }

    setenv("XDG_SESSION_TYPE", sess->type == SESSION_WAYLAND ? "wayland" : "x11", 1);
    setenv("XDG_SESSION_CLASS", "user", 1);
    setenv("XDG_CURRENT_DESKTOP", sess->name, 1);

    setgid(pw ? pw->pw_gid : 1000);
    setuid(pw ? pw->pw_uid : 1000);

    if (strcmp(sess->stratum, "native") != 0 && strcmp(sess->stratum, "global") != 0) {
        char cmd[MAX_SESSION_EXEC + 64];
        snprintf(cmd, sizeof(cmd), "strat %s -- %s", sess->stratum, sess->exec);

        log_msg(0, "Launching session '%s' from stratum '%s' via: %s",
                sess->name, sess->stratum, cmd);

        execlp("strat", "strat", sess->stratum, "--", sess->exec, (char *)NULL);
    }

    execlp("/bin/sh", "/bin/sh", "-c", sess->exec, (char *)NULL);
    _exit(127);
}
