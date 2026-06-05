#include "orbit.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <linux/vt.h>
#include <fcntl.h>
#include <errno.h>

int display_find_free_vt(int preferred) {
    int fd = open("/dev/tty0", O_RDWR);
    if (fd >= 0) {
        int vt = 0;
        if (ioctl(fd, VT_OPENQRY, &vt) == 0 && vt > 0) {
            close(fd);
            return vt;
        }
        close(fd);
    }

    if (preferred > 0) {
        char vtpath[32];
        snprintf(vtpath, sizeof(vtpath), "/dev/tty%d", preferred);
        if (access(vtpath, F_OK) == 0) return preferred;
    }

    for (int i = 1; i <= 63; i++) {
        char vtpath[32];
        snprintf(vtpath, sizeof(vtpath), "/dev/tty%d", i);

        struct stat st;
        if (stat(vtpath, &st) != 0) continue;

        char fdpath[256];
        snprintf(fdpath, sizeof(fdpath), "/sys/class/tty/tty%d/active", i);

        FILE *fp = fopen(fdpath, "r");
        if (fp) {
            char active[16] = {0};
            size_t n = fread(active, 1, sizeof(active) - 1, fp);
            fclose(fp);
            if (n > 0 && active[0] >= '1' && active[0] <= '9') continue;
        }

        return i;
    }

    return 7;
}

int display_switch_vt(int vt) {
    int fd = open("/dev/tty0", O_RDWR);
    if (fd < 0) {
        log_msg(1, "Cannot open /dev/tty0 for VT switch: %s", strerror(errno));
        fd = open("/dev/console", O_RDWR);
    }
    if (fd < 0) return -1;

    ioctl(fd, VT_ACTIVATE, vt);

    struct vt_stat vt_state;
    for (int waited = 0; waited < 50; waited++) {
        if (ioctl(fd, VT_GETSTATE, &vt_state) == 0 && vt_state.v_active == vt) {
            break;
        }
        usleep(100000);
    }

    close(fd);
    return 0;
}

int display_setup_xauth(const char *user, const char *display, orbit_config_t *cfg) {
    char xauth_path[MAX_SESSION_PATH];
    char cmd[1024];
    char cookie[128];
    FILE *fp;

    snprintf(xauth_path, sizeof(xauth_path), "%s-%s", cfg->xauth_path, display);

    fp = popen("mcookie 2>/dev/null || uuidgen 2>/dev/null || echo $(od -An -N16 -tx1 /dev/urandom | tr -d ' ')", "r");
    if (!fp) return -1;

    if (!fgets(cookie, sizeof(cookie), fp)) {
        pclose(fp);
        return -1;
    }
    pclose(fp);

    size_t clen = strlen(cookie);
    if (clen > 0 && cookie[clen - 1] == '\n') cookie[clen - 1] = '\0';

    snprintf(cmd, sizeof(cmd),
             "xauth -f '%s' add ':%s' . '%s' 2>/dev/null",
             xauth_path, display, cookie);

    int ret = system(cmd);

    if (ret != 0) {
        snprintf(cmd, sizeof(cmd),
                 "xauth -f '%s' add ':%s' . '$(dd if=/dev/urandom bs=16 count=1 2>/dev/null | od -An -tx1 | tr -d ' \n')' 2>/dev/null",
                 xauth_path, display);
        ret = system(cmd);
    }

    chmod(xauth_path, 0600);

    log_msg(0, "Xauthority set up at %s for display :%s", xauth_path, display);
    return 0;
}

int display_start_xorg(display_t *disp, orbit_config_t *cfg) {
    pid_t pid;
    char vt_arg[16];
    char display_arg[16];
    char socket_path[64];
    struct stat st;

    if (disp->vt <= 0) {
        disp->vt = display_find_free_vt(cfg->vt_number > 0 ? cfg->vt_number : 0);
    }

    if (disp->display[0] == '\0') {
        int display_num = disp->vt;
        if (display_num < 0) display_num = 0;
        snprintf(disp->display, sizeof(disp->display), "%d", display_num);
    }

    display_setup_xauth(cfg->greeter_user, disp->display, cfg);

    snprintf(vt_arg, sizeof(vt_arg), "vt%d", disp->vt);
    snprintf(display_arg, sizeof(display_arg), ":%s", disp->display);
    snprintf(socket_path, sizeof(socket_path), "/tmp/.X11-unix/X%s", disp->display);

    pid = fork();
    if (pid < 0) return -1;

    if (pid == 0) {
        char xauth_path[MAX_SESSION_PATH];
        snprintf(xauth_path, sizeof(xauth_path), "%s-%s", cfg->xauth_path, disp->display);

        setsid();

        int fd = open("/dev/tty0", O_RDWR);
        if (fd >= 0) {
            ioctl(fd, VT_LOCKSWITCH, 0);
            close(fd);
        }

        execlp(cfg->xorg_path, cfg->xorg_path,
               display_arg,
               vt_arg,
               "-keeptty",
               "-novtswitch",
               "-auth", xauth_path,
               (char *)NULL);

        execlp("Xorg", "Xorg",
               display_arg,
               vt_arg,
               "-keeptty",
               "-novtswitch",
               "-auth", xauth_path,
               (char *)NULL);

        _exit(127);
    }

    disp->xorg_pid = pid;

    int waited = 0;
    while (waited < 50) {
        if (stat(socket_path, &st) == 0) {
            log_msg(0, "Xorg ready on display :%s (PID %d, VT %d, ~%dms)",
                    disp->display, pid, disp->vt, waited * 100);
            return 0;
        }

        int status;
        pid_t wp = waitpid(pid, &status, WNOHANG);
        if (wp == pid) {
            log_msg(1, "Xorg failed to start (exited with status %d)",
                    WIFEXITED(status) ? WEXITSTATUS(status) : -1);
            disp->xorg_pid = 0;
            return -1;
        }

        usleep(100000);
        waited++;
    }

    log_msg(1, "Xorg did not become ready within 5 seconds on display :%s",
            disp->display);
    kill(pid, SIGTERM);
    usleep(200000);
    kill(pid, SIGKILL);
    waitpid(pid, NULL, 0);
    disp->xorg_pid = 0;
    return -1;
}

int display_stop_xorg(display_t *disp) {
    if (disp->xorg_pid > 0) {
        kill(disp->xorg_pid, SIGTERM);
        usleep(200000);
        kill(disp->xorg_pid, SIGKILL);
        waitpid(disp->xorg_pid, NULL, 0);
        disp->xorg_pid = 0;
    }

    if (disp->vt > 0) {
        display_switch_vt(disp->vt);
    }

    return 0;
}
