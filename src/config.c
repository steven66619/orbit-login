/* SPDX-License-Identifier: GPL-3.0-only
 *
 * orbit-login - display manager for Bedrock Linux
 * Copyright (C) 2025  Steven Ende
 */

#include "orbit.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <limits.h>

static int parse_int(const char *s, int default_val) {
    if (!s || !*s) return default_val;
    errno = 0;
    char *end = NULL;
    long val = strtol(s, &end, 10);
    if (errno != 0 || end == s || *end != '\0') return default_val;
    if (val < INT_MIN || val > INT_MAX) return default_val;
    return (int)val;
}

void config_defaults(orbit_config_t *cfg) {
    memset(cfg, 0, sizeof(*cfg));
    cfg->verbose = 0;
    cfg->auto_vt = 1;
    cfg->vt_number = 7;
    snprintf(cfg->xorg_path, sizeof(cfg->xorg_path), "%s", "/usr/bin/Xorg");
    snprintf(cfg->xauth_path, sizeof(cfg->xauth_path), "%s", "/tmp/orbit-xauth");
    snprintf(cfg->session_dir, sizeof(cfg->session_dir), "%s", XSESSIONS_DIR);
    snprintf(cfg->wayland_session_dir, sizeof(cfg->wayland_session_dir), "%s", WAYLAND_SESSIONS_DIR);
    cfg->min_uid = 1000;
    cfg->max_uid = 65000;
    snprintf(cfg->greeter_user, sizeof(cfg->greeter_user), "%s", "root");
    cfg->session_timeout = 30;
}

int config_load(const char *path, orbit_config_t *cfg) {
    config_defaults(cfg);

    const char *config_path = path ? path : CONF_PATH;
    FILE *fp = fopen(config_path, "r");
    if (!fp) {
        log_msg(0, "No config file at %s, using defaults", config_path);
        return 0;
    }

    char line[512];
    char section[64] = "";

    while (fgets(line, sizeof(line), fp)) {
        size_t len = strlen(line);
        if (len > 0 && line[len - 1] == '\n') line[len - 1] = '\0';

        char *p = line;
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '\0' || *p == '#') continue;

        if (*p == '[') {
            char *end = strchr(p, ']');
            if (end) {
                *end = '\0';
                snprintf(section, sizeof(section), "%s", p + 1);
            }
            continue;
        }

        char *eq = strchr(p, '=');
        if (!eq) continue;

        *eq = '\0';
        char *key = p;
        char *val = eq + 1;

        while (*key == ' ' || *key == '\t') key++;
        char *ke = key + strlen(key) - 1;
        while (ke > key && (*ke == ' ' || *ke == '\t')) *ke-- = '\0';

        while (*val == ' ' || *val == '\t') val++;
        char *ve = val + strlen(val) - 1;
        while (ve > val && (*ve == ' ' || *ve == '\t')) *ve-- = '\0';

        if (strcmp(key, "Verbose") == 0)
            cfg->verbose = parse_int(val, 0);
        else if (strcmp(key, "VTNumber") == 0) {
            int v = parse_int(val, 7);
            if (v > 0) {
                cfg->vt_number = v;
                cfg->auto_vt = 0;
            } else {
                log_msg(1, "Ignoring invalid VTNumber=%d, using auto-detect", v);
            }
        } else if (strcmp(key, "XorgPath") == 0)
            snprintf(cfg->xorg_path, sizeof(cfg->xorg_path), "%s", val);
        else if (strcmp(key, "XauthPath") == 0)
            snprintf(cfg->xauth_path, sizeof(cfg->xauth_path), "%s", val);
        else if (strcmp(key, "SessionDir") == 0)
            snprintf(cfg->session_dir, sizeof(cfg->session_dir), "%s", val);
        else if (strcmp(key, "WaylandSessionDir") == 0)
            snprintf(cfg->wayland_session_dir, sizeof(cfg->wayland_session_dir), "%s", val);
        else if (strcmp(key, "MinUid") == 0)
            cfg->min_uid = parse_int(val, 1000);
        else if (strcmp(key, "MaxUid") == 0)
            cfg->max_uid = parse_int(val, 65000);
        else if (strcmp(key, "GreeterUser") == 0)
            snprintf(cfg->greeter_user, sizeof(cfg->greeter_user), "%s", val);
        else if (strcmp(key, "SessionTimeout") == 0)
            cfg->session_timeout = parse_int(val, 30);
    }

    fclose(fp);
    log_msg(0, "Loaded configuration from %s", config_path);
    return 0;
}
