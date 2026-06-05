#include "orbit.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <dirent.h>
#include <errno.h>

#define BRL_PATH "/usr/local/bin/brl"

int strata_list(stratum_t *strata, int max) {
    FILE *fp;
    char line[256];
    int count = 0;

    fp = popen("brl list 2>/dev/null", "r");
    if (!fp) {
        fp = popen(BRL_PATH " list 2>/dev/null", "r");
        if (!fp) return -1;
    }

    while (fgets(line, sizeof(line), fp) && count < max) {
        size_t len = strlen(line);
        if (len > 0) line[len - 1] = '\0';

        if (line[0] == '\0' || line[0] == ' ' || line[0] == '\t')
            continue;

        char *status = NULL;
        char *name = line;

        if (line[0] == '+' || line[0] == '-') {
            status = name;
            name = line + 1;
            while (*name == ' ' || *name == '\t') name++;
        }

        char *end = name;
        while (*end && *end != ' ' && *end != '\t') end++;
        if (*end) *end = '\0';

        if (strlen(name) == 0) continue;

        snprintf(strata[count].name, sizeof(strata[count].name), "%s", name);
        if (status && status[0] == '+') {
            strata[count].enabled = 1;
        } else {
            strata[count].enabled = 0;
        }
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
    char cmd[128];
    FILE *fp;

    snprintf(cmd, sizeof(cmd), "brl which --pid %d 2>/dev/null", pid);
    fp = popen(cmd, "r");
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
    char cmd[256];
    FILE *fp;

    snprintf(cmd, sizeof(cmd), "brl which --bin %s 2>/dev/null", bin);
    fp = popen(cmd, "r");
    if (!fp) return -1;

    if (!fgets(out, outsz, fp)) {
        pclose(fp);
        return -1;
    }

    pclose(fp);

    size_t len = strlen(out);
    if (len > 0 && out[len - 1] == '\n') out[len - 1] = '\0';

    if (strcmp(out, "global") == 0) return 0;

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
