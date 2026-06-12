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
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <X11/Xatom.h>

#define CARD_W          440
#define CARD_H          370
#define FIELD_W         360
#define FIELD_H         40
#define CARD_PAD        40
#define FIELD_GAP       16
#define CARD_RADIUS     10
#define BTN_H           44
#define BTN_W           200

typedef struct {
    Display *dpy;
    Window win;
    GC gc;
    XFontStruct *font;
    XFontStruct *font_bold;
    unsigned long color_bg;
    unsigned long color_card_bg;
    unsigned long color_card_border;
    unsigned long color_fg;
    unsigned long color_fg_dim;
    unsigned long color_accent;
    unsigned long color_accent_hover;
    unsigned long color_field_bg;
    unsigned long color_field_border;
    unsigned long color_field_focus;
    unsigned long color_btn_bg;
    unsigned long color_btn_text;
    unsigned long color_btn_hover;

    session_t sessions[MAX_SESSIONS];
    int nsessions;
    int sel_session;

    user_entry_t users[256];
    int nusers;
    int sel_user;

    char username[64];
    int username_len;

    char password[256];
    int pass_len;

    int connected;
    int sock_fd;

    int focus;
    int authed;
    char status_text[512];
    int status_updated;
    int btn_hover;
    int win_w;
    int win_h;
    int running;
} greeter_state_t;

static unsigned long alloc_color(Display *dpy, int r, int g, int b) {
    Colormap cmap = DefaultColormap(dpy, DefaultScreen(dpy));
    XColor xc;
    xc.red = (r << 8) | r;
    xc.green = (g << 8) | g;
    xc.blue = (b << 8) | b;
    xc.flags = DoRed | DoGreen | DoBlue;
    if (!XAllocColor(dpy, cmap, &xc))
        return BlackPixel(dpy, DefaultScreen(dpy));
    return xc.pixel;
}

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
    ulen = strlen(state->username) + 1;
    memcpy(buf, state->username, ulen);
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
    ulen = strlen(state->username) + 1;
    memcpy(buf + sizeof(session_t), state->username, ulen);
    if (ipc_send(state->sock_fd, IPC_SESSION_START, buf, sizeof(session_t) + ulen) < 0) return -1;
    if (greeter_recv_msg(state->sock_fd, &hdr, buf, sizeof(buf)) < 0) return -1;
    if (hdr.type == IPC_SESSION_STATUS) {
        snprintf(state->status_text, sizeof(state->status_text), "Launched: %.200s", buf);
        state->status_updated = 1;
        return 0;
    }
    return -1;
}

static void draw_round_rect(Display *dpy, Window win, GC gc,
                            int x, int y, int w, int h, int r) {
    int d = r * 2;
    XFillRectangle(dpy, win, gc, x + r, y, w - d, h);
    XFillRectangle(dpy, win, gc, x, y + r, w, h - d);
    XFillArc(dpy, win, gc, x, y, d, d, 90 * 64, 90 * 64);
    XFillArc(dpy, win, gc, x + w - d, y, d, d, 0, 90 * 64);
    XFillArc(dpy, win, gc, x, y + h - d, d, d, 180 * 64, 90 * 64);
    XFillArc(dpy, win, gc, x + w - d, y + h - d, d, d, 270 * 64, 90 * 64);
}

static void render(greeter_state_t *state) {
    int ww = state->win_w;
    int wh = state->win_h;

    int cx = (ww - CARD_W) / 2;
    int cy = (wh - CARD_H) / 2;

    XSetForeground(state->dpy, state->gc, state->color_bg);
    XFillRectangle(state->dpy, state->win, state->gc, 0, 0, ww, wh);

    XSetForeground(state->dpy, state->gc, state->color_card_bg);
    draw_round_rect(state->dpy, state->win, state->gc, cx, cy, CARD_W, CARD_H, CARD_RADIUS);

    XSetForeground(state->dpy, state->gc, state->color_card_border);
    XDrawRectangle(state->dpy, state->win, state->gc, cx, cy, CARD_W, CARD_H);

    int fy = cy + 28;
    int lx = cx + CARD_PAD;
    int fw = FIELD_W;

    XSetFont(state->dpy, state->gc, state->font_bold->fid);
    XSetForeground(state->dpy, state->gc, state->color_fg);
    XDrawString(state->dpy, state->win, state->gc, lx, fy, "orbit-login", 11);

    XSetFont(state->dpy, state->gc, state->font->fid);

    fy += 34;

    if (!state->connected) {
        XSetForeground(state->dpy, state->gc, state->color_fg_dim);
        char *msg1 = "Not connected to orbitd";
        char *msg2 = "Ensure orbitd is running as root";
        XDrawString(state->dpy, state->win, state->gc,
                    cx + (CARD_W - XTextWidth(state->font, msg1, strlen(msg1))) / 2,
                    fy + 30, msg1, strlen(msg1));
        XDrawString(state->dpy, state->win, state->gc,
                    cx + (CARD_W - XTextWidth(state->font, msg2, strlen(msg2))) / 2,
                    fy + 50, msg2, strlen(msg2));
        return;
    }

    int field_x = lx;

    XSetForeground(state->dpy, state->gc, state->color_fg_dim);
    XDrawString(state->dpy, state->win, state->gc, field_x, fy, "Username", 8);
    fy += 14;
    int field_border = (state->focus == 0) ? 2 : 1;
    XSetForeground(state->dpy, state->gc, state->color_field_bg);
    XFillRectangle(state->dpy, state->win, state->gc, field_x, fy, fw, FIELD_H);
    XSetForeground(state->dpy, state->gc, state->focus == 0 ? state->color_field_focus : state->color_field_border);
    for (int b = 0; b < field_border; b++)
        XDrawRectangle(state->dpy, state->win, state->gc,
                       field_x + b, fy + b, fw - b * 2 - 1, FIELD_H - b * 2 - 1);
    if (state->username_len == 0 && state->focus != 0) {
        XSetForeground(state->dpy, state->gc, state->color_fg_dim);
        XDrawString(state->dpy, state->win, state->gc, field_x + 10, fy + FIELD_H / 2 + 5,
                    "username", 8);
    } else if (state->username_len > 0) {
        XSetForeground(state->dpy, state->gc, state->color_fg);
        XDrawString(state->dpy, state->win, state->gc, field_x + 10, fy + FIELD_H / 2 + 5,
                    state->username, state->username_len);
    }
    if (state->focus == 0) {
        int tx = field_x + 10 + XTextWidth(state->font, state->username, state->username_len);
        if ((state->running % 2) == 0) {
            XSetForeground(state->dpy, state->gc, state->color_fg);
            XDrawLine(state->dpy, state->win, state->gc, tx, fy + 8, tx, fy + FIELD_H - 8);
        }
    }
    fy += FIELD_H + FIELD_GAP;

    XSetForeground(state->dpy, state->gc, state->color_fg_dim);
    XDrawString(state->dpy, state->win, state->gc, field_x, fy, "Password", 8);
    fy += 14;
    field_border = (state->focus == 1) ? 2 : 1;
    XSetForeground(state->dpy, state->gc, state->color_field_bg);
    XFillRectangle(state->dpy, state->win, state->gc, field_x, fy, fw, FIELD_H);
    XSetForeground(state->dpy, state->gc, state->focus == 1 ? state->color_field_focus : state->color_field_border);
    for (int b = 0; b < field_border; b++)
        XDrawRectangle(state->dpy, state->win, state->gc,
                       field_x + b, fy + b, fw - b * 2 - 1, FIELD_H - b * 2 - 1);
    if (state->pass_len > 0) {
        XSetForeground(state->dpy, state->gc, state->color_fg);
        int dots = state->pass_len > 55 ? 55 : state->pass_len;
        char pass_disp[64];
        memset(pass_disp, '*', dots);
        pass_disp[dots] = '\0';
        XDrawString(state->dpy, state->win, state->gc, field_x + 10, fy + FIELD_H / 2 + 5,
                    pass_disp, dots);
    } else if (state->focus != 1) {
        XSetForeground(state->dpy, state->gc, state->color_fg_dim);
        XDrawString(state->dpy, state->win, state->gc, field_x + 10, fy + FIELD_H / 2 + 5,
                    "password", 8);
    }
    if (state->focus == 1) {
        int pw_display_len = state->pass_len > 55 ? 55 : state->pass_len;
        int tx = field_x + 10 + XTextWidth(state->font, "**********", pw_display_len > 10 ? 10 : pw_display_len);
        if (pw_display_len < 10)
            tx = field_x + 10 + XTextWidth(state->font, "", 0) + pw_display_len * XTextWidth(state->font, "*", 1);
        else
            tx = field_x + 10 + XTextWidth(state->font, "**********", 10);
        if ((state->running % 2) == 0) {
            XSetForeground(state->dpy, state->gc, state->color_fg);
            XDrawLine(state->dpy, state->win, state->gc, tx, fy + 8, tx, fy + FIELD_H - 8);
        }
    }
    fy += FIELD_H + FIELD_GAP;

    XSetForeground(state->dpy, state->gc, state->color_fg_dim);
    XDrawString(state->dpy, state->win, state->gc, field_x, fy, "Session", 7);
    fy += 14;
    field_border = (state->focus == 2) ? 2 : 1;
    XSetForeground(state->dpy, state->gc, state->color_field_bg);
    XFillRectangle(state->dpy, state->win, state->gc, field_x, fy, fw, FIELD_H);
    XSetForeground(state->dpy, state->gc, state->focus == 2 ? state->color_field_focus : state->color_field_border);
    for (int b = 0; b < field_border; b++)
        XDrawRectangle(state->dpy, state->win, state->gc,
                       field_x + b, fy + b, fw - b * 2 - 1, FIELD_H - b * 2 - 1);

    XSetForeground(state->dpy, state->gc, state->color_fg_dim);
    XDrawString(state->dpy, state->win, state->gc, field_x + 12, fy + FIELD_H / 2 + 5, "<", 1);

    char ses_label[MAX_SESSION_NAME + 16];
    if (state->nsessions > 0 && state->sel_session < state->nsessions) {
        snprintf(ses_label, sizeof(ses_label), "%s",
                 state->sessions[state->sel_session].name);
    } else {
        snprintf(ses_label, sizeof(ses_label), "No sessions");
    }

    XSetForeground(state->dpy, state->gc, state->color_fg);
    int ses_x = field_x + (fw - XTextWidth(state->font, ses_label, strlen(ses_label))) / 2;
    XDrawString(state->dpy, state->win, state->gc, ses_x, fy + FIELD_H / 2 + 5,
                ses_label, strlen(ses_label));

    XSetForeground(state->dpy, state->gc, state->color_fg_dim);
    XDrawString(state->dpy, state->win, state->gc, field_x + fw - 22, fy + FIELD_H / 2 + 5, ">", 1);

    fy += FIELD_H + FIELD_GAP + 8;

    int btn_x = cx + (CARD_W - BTN_W) / 2;
    int btn_y = fy;

    unsigned long btn_bg = state->btn_hover ? state->color_btn_hover : state->color_btn_bg;
    XSetForeground(state->dpy, state->gc, btn_bg);
    draw_round_rect(state->dpy, state->win, state->gc, btn_x, btn_y, BTN_W, BTN_H, 6);

    if (state->focus == 3) {
        XSetForeground(state->dpy, state->gc, state->color_field_focus);
        XDrawRectangle(state->dpy, state->win, state->gc, btn_x, btn_y, BTN_W, BTN_H);
    }

    XSetFont(state->dpy, state->gc, state->font_bold->fid);
    XSetForeground(state->dpy, state->gc, state->color_btn_text);
    const char *btn_text = "Sign In";
    int btn_text_x = btn_x + (BTN_W - XTextWidth(state->font_bold, btn_text, strlen(btn_text))) / 2;
    XDrawString(state->dpy, state->win, state->gc, btn_text_x, btn_y + BTN_H / 2 + 5,
                btn_text, strlen(btn_text));
    XSetFont(state->dpy, state->gc, state->font->fid);

    if (state->status_text[0]) {
        XSetForeground(state->dpy, state->gc, state->color_accent);
        int st_y = cy + CARD_H - 14;
        XDrawString(state->dpy, state->win, state->gc, lx, st_y,
                    state->status_text, strlen(state->status_text));
    }

    char hint_buf[128];
    snprintf(hint_buf, sizeof(hint_buf), "%s",
             state->focus == 3 ? "Press Enter to sign in" :
             "[Tab] Next  [Enter] Confirm  [Esc] Exit");
    XSetForeground(state->dpy, state->gc, state->color_fg_dim);
    int hint_y = wh - 20;
    int hint_x = (ww - XTextWidth(state->font, hint_buf, strlen(hint_buf))) / 2;
    XDrawString(state->dpy, state->win, state->gc, hint_x, hint_y,
                hint_buf, strlen(hint_buf));
}

static void handle_key(greeter_state_t *state, KeySym keysym, char keychar) {
    if (!state->connected) return;

    switch (keysym) {
    case XK_Escape:
        if (state->focus > 0) {
            state->focus--;
        } else {
            state->running = 0;
        }
        break;
    case XK_Tab:
        state->focus = (state->focus + 1) % 4;
        break;
    case XK_Up:
        if (state->focus == 2) {
            if (state->sel_session > 0) state->sel_session--;
        }
        break;
    case XK_Down:
        if (state->focus == 2) {
            if (state->sel_session < state->nsessions - 1) state->sel_session++;
        }
        break;
    case XK_Left:
        if (state->focus == 2) {
            if (state->sel_session > 0) state->sel_session--;
        }
        break;
    case XK_Right:
        if (state->focus == 2) {
            if (state->sel_session < state->nsessions - 1) state->sel_session++;
        }
        break;
    case XK_Return:
    case XK_KP_Enter:
        if (state->focus < 2) {
            state->focus++;
        } else if (state->focus == 2 || state->focus == 3) {
            if (state->username_len == 0) {
                snprintf(state->status_text, sizeof(state->status_text),
                         "Please enter a username");
                state->focus = 0;
                break;
            }
            if (state->pass_len == 0) {
                snprintf(state->status_text, sizeof(state->status_text),
                         "Please enter a password");
                state->focus = 1;
                break;
            }
            if (state->nsessions == 0) {
                snprintf(state->status_text, sizeof(state->status_text),
                         "No sessions available");
                break;
            }
            snprintf(state->status_text, sizeof(state->status_text),
                     "Authenticating...");
            render(state);
            XFlush(state->dpy);

            if (greeter_authenticate(state) == 0) {
                state->authed = 1;
                snprintf(state->status_text, sizeof(state->status_text),
                         "OK! Starting '%s'...",
                         state->sessions[state->sel_session].name);
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
        if (state->focus == 0) {
            if (state->username_len > 0) state->username[--state->username_len] = '\0';
        } else if (state->focus == 1) {
            if (state->pass_len > 0) state->password[--state->pass_len] = '\0';
        }
        break;
    default:
        if (keychar >= 32 && keychar <= 126) {
            if (state->focus == 0 && state->username_len < 63) {
                state->username[state->username_len++] = keychar;
                state->username[state->username_len] = '\0';
            } else if (state->focus == 1 && state->pass_len < 255) {
                state->password[state->pass_len++] = keychar;
                state->password[state->pass_len] = '\0';
            }
        }
        break;
    }
}

int main(int argc, char **argv) {
    greeter_state_t state;
    XEvent ev;
    int screen;

    memset(&state, 0, sizeof(state));
    state.sel_user = 0;
    state.sel_session = 0;
    state.focus = 0;
    state.username_len = 0;
    state.sock_fd = -1;
    state.btn_hover = 0;
    state.running = 1;

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
    Window root = RootWindow(state.dpy, screen);

    const char *font_names[] = {
        "10x20",
        "9x15",
        "8x13",
        "fixed",
        NULL
    };

    state.font = NULL;
    for (int i = 0; font_names[i]; i++) {
        state.font = XLoadQueryFont(state.dpy, font_names[i]);
        if (state.font) break;
    }
    if (!state.font) {
        fprintf(stderr, "Cannot load any font\n");
        return 1;
    }

    const char *bold_font_names[] = {
        "10x20",
        "9x15bold",
        "fixed",
        NULL
    };
    state.font_bold = NULL;
    for (int i = 0; bold_font_names[i]; i++) {
        state.font_bold = XLoadQueryFont(state.dpy, bold_font_names[i]);
        if (state.font_bold) break;
    }
    if (!state.font_bold) state.font_bold = state.font;

    state.color_bg = alloc_color(state.dpy, 17, 17, 20);
    state.color_card_bg = alloc_color(state.dpy, 28, 28, 32);
    state.color_card_border = alloc_color(state.dpy, 48, 48, 54);
    state.color_fg = alloc_color(state.dpy, 220, 220, 224);
    state.color_fg_dim = alloc_color(state.dpy, 110, 110, 115);
    state.color_accent = alloc_color(state.dpy, 74, 144, 217);
    state.color_accent_hover = alloc_color(state.dpy, 94, 164, 237);
    state.color_field_bg = alloc_color(state.dpy, 22, 22, 26);
    state.color_field_border = alloc_color(state.dpy, 55, 55, 60);
    state.color_field_focus = alloc_color(state.dpy, 74, 144, 217);
    state.color_btn_bg = alloc_color(state.dpy, 74, 144, 217);
    state.color_btn_text = alloc_color(state.dpy, 255, 255, 255);
    state.color_btn_hover = alloc_color(state.dpy, 94, 164, 237);

    state.win_w = DisplayWidth(state.dpy, screen);
    state.win_h = DisplayHeight(state.dpy, screen);

    XSetWindowAttributes attrs;
    attrs.background_pixel = state.color_bg;
    attrs.event_mask = ExposureMask | KeyPressMask | KeyReleaseMask |
                       StructureNotifyMask | ButtonPressMask | PointerMotionMask;

    state.win = XCreateWindow(state.dpy, root, 0, 0, state.win_w, state.win_h, 0,
                              CopyFromParent, InputOutput, CopyFromParent,
                              CWBackPixel | CWEventMask, &attrs);

    state.gc = XCreateGC(state.dpy, state.win, 0, NULL);

    Atom wm_state = XInternAtom(state.dpy, "_NET_WM_STATE", False);
    Atom wm_fullscreen = XInternAtom(state.dpy, "_NET_WM_STATE_FULLSCREEN", False);
    XChangeProperty(state.dpy, state.win, wm_state, XA_ATOM, 32,
                    PropModeReplace, (unsigned char *)&wm_fullscreen, 1);

    XStoreName(state.dpy, state.win, "orbit-login");

    Atom wm_delete = XInternAtom(state.dpy, "WM_DELETE_WINDOW", False);
    XSetWMProtocols(state.dpy, state.win, &wm_delete, 1);

    XMapRaised(state.dpy, state.win);
    XFlush(state.dpy);

    if (XGrabKeyboard(state.dpy, state.win, True, GrabModeAsync, GrabModeAsync, CurrentTime) != GrabSuccess) {
        fprintf(stderr, "Warning: cannot grab keyboard\n");
    }

    if (greeter_connect(&state) == 0) {
        greeter_fetch_users(&state);
        greeter_fetch_sessions(&state);
    } else {
        snprintf(state.status_text, sizeof(state.status_text),
                 "Cannot connect to orbitd. Is it running?");
    }

    while (state.running) {
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
            case MotionNotify: {
                int mx = ev.xmotion.x;
                int my = ev.xmotion.y;
                int cx = (state.win_w - CARD_W) / 2;
                int cy = (state.win_h - CARD_H) / 2;
                int btn_x = cx + (CARD_W - BTN_W) / 2;
                int btn_y = cy + CARD_H - BTN_H - 40;
                int old_hover = state.btn_hover;
                state.btn_hover = (mx >= btn_x && mx <= btn_x + BTN_W &&
                                   my >= btn_y && my <= btn_y + BTN_H);
                if (old_hover != state.btn_hover) render(&state);
                break;
            }
            case ButtonPress: {
                int mx = ev.xbutton.x;
                int my = ev.xbutton.y;
                int cx = (state.win_w - CARD_W) / 2;
                int cy = (state.win_h - CARD_H) / 2;
                int lx = cx + CARD_PAD;
                int fw = FIELD_W;
                int y = cy + 28 + 34;
                if (mx >= lx && mx <= lx + fw && my >= y + 14 && my <= y + 14 + FIELD_H) {
                    state.focus = 0;
                }
                y += FIELD_H + FIELD_GAP;
                if (mx >= lx && mx <= lx + fw && my >= y + 14 && my <= y + 14 + FIELD_H) {
                    state.focus = 1;
                }
                y += FIELD_H + FIELD_GAP;
                if (mx >= lx && mx <= lx + fw && my >= y + 14 && my <= y + 14 + FIELD_H) {
                    state.focus = 2;
                    int rel_x = mx - lx;
                    if (rel_x < 40) {
                        if (state.sel_session > 0) state.sel_session--;
                    } else if (rel_x > fw - 40) {
                        if (state.sel_session < state.nsessions - 1) state.sel_session++;
                    }
                }
                y += FIELD_H + FIELD_GAP + 8;
                int btn_x = cx + (CARD_W - BTN_W) / 2;
                int btn_y = y;
                if (mx >= btn_x && mx <= btn_x + BTN_W &&
                    my >= btn_y && my <= btn_y + BTN_H) {
                    state.focus = 3;
                    handle_key(&state, XK_Return, 0);
                }
                render(&state);
                break;
            }
            case ClientMessage:
                if ((Atom)ev.xclient.data.l[0] == wm_delete)
                    state.running = 0;
                break;
            case ConfigureNotify:
                state.win_w = ev.xconfigure.width;
                state.win_h = ev.xconfigure.height;
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
