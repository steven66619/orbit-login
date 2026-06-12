/* SPDX-License-Identifier: GPL-3.0-only
 *
 * orbit-login - display manager for Bedrock Linux
 * Copyright (C) 2025  Steven Ende
 */

#include "orbit.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <dirent.h>
#include <errno.h>
#include <unistd.h>

static const char *brl_paths[] = {
    "/bedrock/bin/brl",
    "/usr/local/bin/brl",
    NULL
};

static const char *find_brl(void) {
    for (int i = 0; brl_paths[i]; i++) {
        if (access(brl_paths[i], X_OK) == 0)
            return brl_paths[i];
    }
    return NULL;
}

int strata_list(stratum_t *strata, int max) {
    const char *brl = find_brl();
    if (!brl) return -1;

    char cmd[256];
    snprintf(cmd, sizeof(cmd), "%s list 2>/dev/null", brl);

    FILE *fp = popen(cmd, "r");
    if (!fp) return -1;

    char line[256];
    int count = 0;

    while (fgets(line, sizeof(line), fp) && count < max) {
        size_t len = strlen(line);
        while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r'))
            line[--len] = '\0';

        if (len == 0) continue;

        snprintf(strata[count].name, sizeof(strata[count].name), "%s", line);
        strata[count].enabled = 1;
        strata[count].hidden = 0;
        count++;
    }

    pclose(fp);
    return count;
}

int strata_list_enabled(stratum_t *strata, int max) {
    stratum_t all[MAX_STRATA];
    int n = strata_list(all, MAX_STRATA);
    int count = 0;

    for (int i = 0; i < n && count < max; i++) {
        if (all[i].enabled) {
            memcpy(&strata[count], &all[i], sizeof(stratum_t));
            count++;
        }
    }
    return count;
}

int strata_which_pid(pid_t pid, char *out, size_t outsz) {
    const char *brl = find_brl();
    if (!brl) return -1;

    char cmd[256];
    snprintf(cmd, sizeof(cmd), "%s which --pid %d 2>/dev/null", brl, pid);
    FILE *fp = popen(cmd, "r");
    if (!fp) return -1;

    if (!fgets(out, outsz, fp)) {
        pclose(fp);
        return -1;
    }

    pclose(fp);

    size_t len = strlen(out);
    if (len > 0 && out[len - 1] == '\n') out[len - 1] = '\0';

    return 0;
}

int strata_which_bin(const char *bin, char *out, size_t outsz) {
    const char *brl = find_brl();
    if (!brl) return -1;

    char cmd[512];
    snprintf(cmd, sizeof(cmd), "%s which --bin %s 2>/dev/null", brl, bin);
    FILE *fp = popen(cmd, "r");
    if (!fp) return -1;

    if (!fgets(out, outsz, fp)) {
        pclose(fp);
        return -1;
    }

    pclose(fp);

    size_t len = strlen(out);
    if (len > 0 && out[len - 1] == '\n') out[len - 1] = '\0';

    return 0;
}

int strata_find_session_file(const char *desktop, char *stratum, size_t stratum_sz) {
    stratum_t strata[MAX_STRATA];
    int n = strata_list_enabled(strata, MAX_STRATA);
    char path[MAX_SESSION_PATH];

    for (int i = 0; i < n; i++) {
        snprintf(path, sizeof(path),
                 BEDROCK_STRATA_DIR "/%s/usr/share/xsessions/%s",
                 strata[i].name, desktop);

        if (access(path, F_OK) == 0) {
            snprintf(stratum, stratum_sz, "%s", strata[i].name);
            return 0;
        }

        snprintf(path, sizeof(path),
                 BEDROCK_STRATA_DIR "/%s/usr/share/wayland-sessions/%s",
                 strata[i].name, desktop);

        if (access(path, F_OK) == 0) {
            snprintf(stratum, stratum_sz, "%s", strata[i].name);
            return 0;
        }
    }

    snprintf(stratum, stratum_sz, "native");
    return 1;
}

int strata_resolve_path(const char *stratum, const char *path, char *out, size_t outsz) {
    if (strcmp(stratum, "native") == 0 || strcmp(stratum, "global") == 0) {
        snprintf(out, outsz, "%s", path);
        return 0;
    }

    if (path[0] != '/') {
        snprintf(out, outsz, "%s", path);
        return 0;
    }

    snprintf(out, outsz, BEDROCK_STRATA_DIR "/%s%s", stratum, path);
    return 0;
}
