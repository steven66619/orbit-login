#include "orbit.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <X11/Xatom.h>

#define WIN_W 800
#define WIN_H 600
#define FIELD_W 400
#define LINE_H 20
#define CHAR_W 8
#define CHAR_H 16

typedef struct {
    Display *dpy;
    Window win;
    GC gc;
    XFontStruct *font;
    XFontStruct *font_bold;
    unsigned long color_bg;
    unsigned long color_fg;
    unsigned long color_accent;
    unsigned long color_dim;
    unsigned long color_input_bg;

    session_t sessions[MAX_SESSIONS];
    int nsessions;
    int sel_session;

    user_entry_t users[256];
    int nusers;
    int sel_user;

    char password[256];
    int pass_len;

    int connected;
    int sock_fd;

    int focus;
    int authed;
    char status_text[512];
    int status_updated;
} greeter_state_t;

static int greeter_connect(greeter_state_t *state) {
    struct sockaddr_un addr;

    state->sock_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (state->sock_fd < 0) return -1;

    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", SOCKET_PATH);

    if (connect(state->sock_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(state->sock_fd);
        state->sock_fd = -1;
        return -1;
    }

    state->connected = 1;
    return 0;
}

static int greeter_recv_msg(int fd, ipc_header_t *hdr, void *payload, uint32_t maxsz) {
    return ipc_recv(fd, hdr, payload, maxsz);
}

static int greeter_fetch_users(greeter_state_t *state) {
    ipc_header_t hdr;
    if (!state->connected) return -1;

    if (ipc_send(state->sock_fd, IPC_USER_LIST, NULL, 0) < 0) return -1;
    if (greeter_recv_msg(state->sock_fd, &hdr, state->users, sizeof(user_entry_t) * 256) < 0) return -1;
    if (hdr.type != IPC_USER_LIST) return -1;

    state->nusers = hdr.payload_len / sizeof(user_entry_t);
    if (state->sel_user >= state->nusers) state->sel_user = 0;
    return state->nusers;
}

static int greeter_fetch_sessions(greeter_state_t *state) {
    ipc_header_t hdr;
    if (!state->connected) return -1;

    if (ipc_send(state->sock_fd, IPC_SESSION_LIST, NULL, 0) < 0) return -1;
    if (greeter_recv_msg(state->sock_fd, &hdr, state->sessions, sizeof(session_t) * MAX_SESSIONS) < 0) return -1;
    if (hdr.type != IPC_SESSION_LIST) return -1;

    state->nsessions = hdr.payload_len / sizeof(session_t);
    if (state->sel_session >= state->nsessions) state->sel_session = 0;
    return state->nsessions;
}

static int greeter_authenticate(greeter_state_t *state) {
    ipc_header_t hdr;
    char buf[256 + 256 + 2];
    char result;
    size_t ulen;

    if (!state->connected) return -1;
    ulen = strlen(state->users[state->sel_user].name) + 1;
    memcpy(buf, state->users[state->sel_user].name, ulen);
    memcpy(buf + ulen, state->password, state->pass_len + 1);

    if (ipc_send(state->sock_fd, IPC_AUTH_REQ, buf, ulen + state->pass_len + 1) < 0) return -1;
    if (greeter_recv_msg(state->sock_fd, &hdr, &result, sizeof(result)) < 0) return -1;
    if (hdr.type != IPC_AUTH_RES) return -1;

    return (result == '1') ? 0 : -1;
}

static int greeter_start_session(greeter_state_t *state) {
    ipc_header_t hdr;
    char buf[sizeof(session_t) + 64];
    size_t ulen;

    if (!state->connected) return -1;
    memcpy(buf, &state->sessions[state->sel_session], sizeof(session_t));
    ulen = strlen(state->users[state->sel_user].name) + 1;
    memcpy(buf + sizeof(session_t), state->users[state->sel_user].name, ulen);

    if (ipc_send(state->sock_fd, IPC_SESSION_START, buf, sizeof(session_t) + ulen) < 0) return -1;
    if (greeter_recv_msg(state->sock_fd, &hdr, buf, sizeof(buf)) < 0) return -1;

    if (hdr.type == IPC_SESSION_STATUS) {
        snprintf(state->status_text, sizeof(state->status_text), "Launched: %.200s", buf);
        state->status_updated = 1;
        return 0;
    }
    return -1;
}

static void render(greeter_state_t *state) {
    XWindowAttributes wa;
    char line[512];
    int y;

    XGetWindowAttributes(state->dpy, state->win, &wa);
    int ww = wa.width;
    int wh = wa.height;

    XSetForeground(state->dpy, state->gc, state->color_bg);
    XFillRectangle(state->dpy, state->win, state->gc, 0, 0, ww, wh);

    XSetForeground(state->dpy, state->gc, state->color_accent);
    XDrawString(state->dpy, state->win, state->gc, 40, 32, "orbit-login", 11);

    y = 70;

    if (!state->connected) {
        XSetForeground(state->dpy, state->gc, state->color_dim);
        XDrawString(state->dpy, state->win, state->gc, 40, y, "Not connected to orbitd", 23);
        y += LINE_H;
        XDrawString(state->dpy, state->win, state->gc, 40, y, "Ensure orbitd is running as root", 33);
        return;
    }

    XSetForeground(state->dpy, state->gc, state->focus == 0 ? state->color_accent : state->color_fg);
    XDrawString(state->dpy, state->win, state->gc, 40, y, "User:", 5);
    y += 6;

    for (int i = 0; i < state->nusers && i < 8; i++) {
        snprintf(line, sizeof(line), "  %s  (%s)", state->users[i].name, state->users[i].realname);
        if (i == state->sel_user) {
            XSetForeground(state->dpy, state->gc, state->color_accent);
            XFillRectangle(state->dpy, state->win, state->gc, 36, y - 12, 400, LINE_H);
            XSetForeground(state->dpy, state->gc, state->color_bg);
            line[0] = '>';
            XDrawString(state->dpy, state->win, state->gc, 40, y, line, strlen(line));
        } else {
            XSetForeground(state->dpy, state->gc, state->color_dim);
            XDrawString(state->dpy, state->win, state->gc, 40, y, line, strlen(line));
        }
        y += LINE_H;
    }
    y += 10;

    XSetForeground(state->dpy, state->gc, state->focus == 1 ? state->color_accent : state->color_fg);
    XDrawString(state->dpy, state->win, state->gc, 40, y, "Password:", 9);
    y += 6;

    char pass_disp[256];
    if (state->pass_len > 0) {
        memset(pass_disp, '*', state->pass_len);
        pass_disp[state->pass_len] = '\0';
    } else {
        snprintf(pass_disp, sizeof(pass_disp), "[enter password]");
    }

    XSetForeground(state->dpy, state->gc, state->color_input_bg);
    XFillRectangle(state->dpy, state->win, state->gc, 36, y - 12, 400, LINE_H);
    XSetForeground(state->dpy, state->gc, state->pass_len > 0 ? state->color_fg : state->color_dim);
    XDrawString(state->dpy, state->win, state->gc, 40, y, pass_disp, strlen(pass_disp));
    y += LINE_H + 10;

    XSetForeground(state->dpy, state->gc, state->focus == 2 ? state->color_accent : state->color_fg);
    XDrawString(state->dpy, state->win, state->gc, 40, y, "Session:", 8);
    y += 6;

    for (int i = 0; i < state->nsessions; i++) {
        char ses_buf[384];
        char *stratum_tag = state->sessions[i].stratum;
        if (strcmp(stratum_tag, "native") == 0) {
            snprintf(ses_buf, sizeof(ses_buf), "  %s", state->sessions[i].name);
        } else {
            snprintf(ses_buf, sizeof(ses_buf), "  %s  [%s]",
                     state->sessions[i].name, stratum_tag);
        }
        if (i == state->sel_session) {
            XSetForeground(state->dpy, state->gc, state->color_accent);
            XFillRectangle(state->dpy, state->win, state->gc, 36, y - 12, ww - 72, LINE_H);
            XSetForeground(state->dpy, state->gc, state->color_bg);
            ses_buf[0] = '>';
            XDrawString(state->dpy, state->win, state->gc, 40, y, ses_buf, strlen(ses_buf));
        } else {
            XSetForeground(state->dpy, state->gc, state->color_fg);
            XDrawString(state->dpy, state->win, state->gc, 40, y, ses_buf, strlen(ses_buf));
        }
        y += LINE_H;
    }

    if (state->status_text[0]) {
        y += 10;
        XSetForeground(state->dpy, state->gc, state->color_accent);
        XDrawString(state->dpy, state->win, state->gc, 40, y,
                    state->status_text, strlen(state->status_text));
    }

    y = wh - 24;
    XSetForeground(state->dpy, state->gc, state->color_dim);
    XDrawString(state->dpy, state->win, state->gc, 40, y,
                "[Enter] Login  [Tab] Cycle focus  [Up/Down] Navigate  [Esc] Quit", 67);

    if (state->authed && state->status_updated) {
        sleep(2);
        state->status_updated = 0;
    }
}

static void handle_key(greeter_state_t *state, KeySym keysym, char keychar) {
    if (!state->connected) return;

    switch (keysym) {
    case XK_Escape:
        exit(1);
    case XK_Tab:
        state->focus = (state->focus + 1) % 3;
        break;
    case XK_Up:
        if (state->focus == 0) {
            if (state->sel_user > 0) state->sel_user--;
        } else if (state->focus == 2) {
            if (state->sel_session > 0) state->sel_session--;
        }
        break;
    case XK_Down:
        if (state->focus == 0) {
            if (state->sel_user < state->nusers - 1) state->sel_user++;
        } else if (state->focus == 2) {
            if (state->sel_session < state->nsessions - 1) state->sel_session++;
        }
        break;
    case XK_Return:
        if (state->pass_len > 0 && state->nusers > 0 && state->nsessions > 0) {
            snprintf(state->status_text, sizeof(state->status_text),
                     "Authenticating...");
            render(state);
            XFlush(state->dpy);

            if (greeter_authenticate(state) == 0) {
                state->authed = 1;
                snprintf(state->status_text, sizeof(state->status_text),
                         "OK! Starting '%s' (%s)...",
                         state->sessions[state->sel_session].name,
                         state->sessions[state->sel_session].stratum);
                render(state);
                XFlush(state->dpy);
                if (greeter_start_session(state) == 0) {
                    sleep(2);
                    exit(0);
                }
                snprintf(state->status_text, sizeof(state->status_text),
                         "Failed to start session.");
                memset(state->password, 0, state->pass_len);
                state->pass_len = 0;
            } else {
                snprintf(state->status_text, sizeof(state->status_text),
                         "Authentication failed.");
                memset(state->password, 0, state->pass_len);
                state->pass_len = 0;
            }
        }
        break;
    case XK_BackSpace:
        if (state->pass_len > 0) state->password[--state->pass_len] = '\0';
        break;
    default:
        if (keychar >= 32 && keychar <= 126 && state->pass_len < 255) {
            state->password[state->pass_len++] = keychar;
            state->password[state->pass_len] = '\0';
        }
        break;
    }
}

int main(int argc, char **argv) {
    greeter_state_t state;
    XEvent ev;
    int screen;
    Window root;

    memset(&state, 0, sizeof(state));
    state.sel_user = 0;
    state.sel_session = 0;
    state.focus = 0;
    state.sock_fd = -1;

    for (int attempt = 0; attempt < 30; attempt++) {
        state.dpy = XOpenDisplay(NULL);
        if (state.dpy) break;
        if (attempt == 0) fprintf(stderr, "Waiting for X display...\n");
        usleep(200000);
    }
    if (!state.dpy) {
        fprintf(stderr, "Cannot open X display after 6 seconds\n");
        return 1;
    }

    screen = DefaultScreen(state.dpy);
    root = RootWindow(state.dpy, screen);

    state.font = XLoadQueryFont(state.dpy, "fixed");
    if (!state.font) state.font = XLoadQueryFont(state.dpy, "9x15");
    if (!state.font) state.font = XLoadQueryFont(state.dpy, "6x13");
    if (!state.font) {
        fprintf(stderr, "Cannot load any font\n");
        return 1;
    }
    state.font_bold = state.font;

    state.color_bg = BlackPixel(state.dpy, screen);
    state.color_fg = WhitePixel(state.dpy, screen);
    state.color_accent = 0x44aa44;
    state.color_dim = 0x666666;
    state.color_input_bg = 0x222222;

    Colormap cmap = DefaultColormap(state.dpy, screen);
    XColor xc;

    xc.red = 0x2222; xc.green = 0x5555; xc.blue = 0x2222;
    xc.flags = DoRed | DoGreen | DoBlue;
    XAllocColor(state.dpy, cmap, &xc);
    state.color_accent = xc.pixel;

    xc.red = 0x4444; xc.green = 0x4444; xc.blue = 0x4444;
    xc.flags = DoRed | DoGreen | DoBlue;
    XAllocColor(state.dpy, cmap, &xc);
    state.color_dim = xc.pixel;

    xc.red = 0x1111; xc.green = 0x1111; xc.blue = 0x1111;
    xc.flags = DoRed | DoGreen | DoBlue;
    XAllocColor(state.dpy, cmap, &xc);
    state.color_input_bg = xc.pixel;

    XSetWindowAttributes attrs;
    attrs.background_pixel = state.color_bg;
    attrs.event_mask = ExposureMask | KeyPressMask | KeyReleaseMask |
                       StructureNotifyMask | ButtonPressMask;

    state.win = XCreateWindow(state.dpy, root, 0, 0, WIN_W, WIN_H, 0,
                              CopyFromParent, InputOutput, CopyFromParent,
                              CWBackPixel | CWEventMask, &attrs);

    state.gc = XCreateGC(state.dpy, state.win, 0, NULL);

    XStoreName(state.dpy, state.win, "orbit-login");
    XMapRaised(state.dpy, state.win);
    XFlush(state.dpy);

    XGrabKeyboard(state.dpy, state.win, True, GrabModeAsync, GrabModeAsync, CurrentTime);

    if (greeter_connect(&state) == 0) {
        greeter_fetch_users(&state);
        greeter_fetch_sessions(&state);
    } else {
        snprintf(state.status_text, sizeof(state.status_text),
                 "Cannot connect to orbitd. Is it running?");
    }

    Atom wm_delete = XInternAtom(state.dpy, "WM_DELETE_WINDOW", False);
    XSetWMProtocols(state.dpy, state.win, &wm_delete, 1);

    int running = 1;
    while (running) {
        while (XPending(state.dpy) > 0) {
            XNextEvent(state.dpy, &ev);
            switch (ev.type) {
            case Expose:
                if (ev.xexpose.count == 0) render(&state);
                break;
            case KeyPress: {
                KeySym keysym;
                char buf[8] = {0};
                XLookupString(&ev.xkey, buf, sizeof(buf), &keysym, NULL);
                handle_key(&state, keysym, buf[0]);
                render(&state);
                break;
            }
            case ClientMessage:
                if ((Atom)ev.xclient.data.l[0] == wm_delete) running = 0;
                break;
            case ConfigureNotify:
                render(&state);
                break;
            }
        }
        usleep(50000);
    }

    if (state.sock_fd >= 0) close(state.sock_fd);
    XFreeGC(state.dpy, state.gc);
    XDestroyWindow(state.dpy, state.win);
    XCloseDisplay(state.dpy);
    return 0;
}
