/* SPDX-License-Identifier: GPL-3.0-only
 *
 * orbit-login - display manager for Bedrock Linux
 * Copyright (C) 2025  Steven Ende
 */

#include "orbit.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <security/pam_appl.h>
#include <unistd.h>
#include <sys/types.h>
#include <pwd.h>

struct auth_ctx {
    const char *user;
    const char *pass;
};

static void auth_conv_free_resp(struct pam_response *resp, int count) {
    if (!resp) return;
    for (int i = 0; i < count; i++) {
        free(resp[i].resp);
    }
    free(resp);
}

static int auth_conv(int nmsg, const struct pam_message **msg,
                     struct pam_response **resp, void *appdata) {
    struct auth_ctx *ctx = appdata;
    int i;

    *resp = calloc(nmsg, sizeof(struct pam_response));
    if (!*resp) return PAM_BUF_ERR;

    for (i = 0; i < nmsg; i++) {
        switch (msg[i]->msg_style) {
        case PAM_PROMPT_ECHO_OFF:
            (*resp)[i].resp = strdup(ctx->pass ? ctx->pass : "");
            if (!(*resp)[i].resp) {
                auth_conv_free_resp(*resp, i + 1);
                *resp = NULL;
                return PAM_BUF_ERR;
            }
            break;
        case PAM_PROMPT_ECHO_ON:
            (*resp)[i].resp = strdup(ctx->user ? ctx->user : "");
            if (!(*resp)[i].resp) {
                auth_conv_free_resp(*resp, i + 1);
                *resp = NULL;
                return PAM_BUF_ERR;
            }
            break;
        case PAM_ERROR_MSG:
            fprintf(stderr, "PAM: %s\n", msg[i]->msg);
            break;
        case PAM_TEXT_INFO:
            break;
        }
    }
    return PAM_SUCCESS;
}

int auth_authenticate(const char *user, const char *pass, const display_t *disp, orbit_config_t *cfg) {
    (void)disp;
    (void)cfg;
    pam_handle_t *pamh = NULL;
    int ret;
    struct auth_ctx ctx;
    struct pam_conv conv;
    const char *service_name = "orbit-login";

    if (!user || !*user) return -1;
    if (!pass) pass = "";

    ctx.user = user;
    ctx.pass = pass;
    conv.conv = auth_conv;
    conv.appdata_ptr = &ctx;

    ret = pam_start(service_name, user, &conv, &pamh);
    if (ret != PAM_SUCCESS) {
        log_msg(1, "pam_start failed: %s", pam_strerror(pamh, ret));
        return -1;
    }

    ret = pam_authenticate(pamh, 0);
    if (ret != PAM_SUCCESS) {
        log_msg(1, "pam_authenticate failed for '%s': %s", user, pam_strerror(pamh, ret));
        pam_end(pamh, ret);
        return -1;
    }

    ret = pam_acct_mgmt(pamh, 0);
    if (ret != PAM_SUCCESS) {
        log_msg(1, "pam_acct_mgmt failed for '%s': %s", user, pam_strerror(pamh, ret));
        pam_end(pamh, ret);
        return -1;
    }

    pam_end(pamh, PAM_SUCCESS);
    log_msg(0, "Authentication successful for '%s'", user);
    return 0;
}

int auth_session_open(const char *user, const display_t *disp, orbit_config_t *cfg) {
    pam_handle_t *pamh = NULL;
    int ret;
    struct auth_ctx ctx;
    struct pam_conv conv;
    const char *service_name = "orbit-login";

    if (!user || !*user) return -1;

    ctx.user = user;
    ctx.pass = NULL;
    conv.conv = auth_conv;
    conv.appdata_ptr = &ctx;

    ret = pam_start(service_name, user, &conv, &pamh);
    if (ret != PAM_SUCCESS) return -1;

    char display_env[64];
    snprintf(display_env, sizeof(display_env), "DISPLAY=:%s", disp->display);
    if (pam_putenv(pamh, display_env) != PAM_SUCCESS) {
        log_msg(1, "pam_putenv DISPLAY failed");
    }

    char xauth_env[256];
    snprintf(xauth_env, sizeof(xauth_env), "XAUTHORITY=%s-%s", cfg->xauth_path, disp->display);
    if (pam_putenv(pamh, xauth_env) != PAM_SUCCESS) {
        log_msg(1, "pam_putenv XAUTHORITY failed");
    }

    ret = pam_open_session(pamh, 0);
    if (ret != PAM_SUCCESS) {
        log_msg(1, "pam_open_session failed: %s", pam_strerror(pamh, ret));
        pam_end(pamh, ret);
        return -1;
    }

    pam_end(pamh, PAM_SUCCESS);
    return 0;
}

int auth_session_close(const char *user, const display_t *disp, orbit_config_t *cfg) {
    (void)disp;
    (void)cfg;
    pam_handle_t *pamh = NULL;
    int ret;
    struct auth_ctx ctx;
    struct pam_conv conv;
    const char *service_name = "orbit-login";

    if (!user || !*user) return -1;

    ctx.user = user;
    ctx.pass = NULL;
    conv.conv = auth_conv;
    conv.appdata_ptr = &ctx;

    ret = pam_start(service_name, user, &conv, &pamh);
    if (ret != PAM_SUCCESS) return -1;

    ret = pam_close_session(pamh, 0);
    if (ret != PAM_SUCCESS) {
        log_msg(1, "pam_close_session failed: %s", pam_strerror(pamh, ret));
    }

    pam_end(pamh, ret);
    return (ret == PAM_SUCCESS) ? 0 : -1;
}

int auth_set_cred(const char *user, const display_t *disp, orbit_config_t *cfg) {
    (void)disp;
    (void)cfg;
    pam_handle_t *pamh = NULL;
    int ret;
    struct auth_ctx ctx;
    struct pam_conv conv;
    const char *service_name = "orbit-login";

    if (!user || !*user) return -1;

    ctx.user = user;
    ctx.pass = NULL;
    conv.conv = auth_conv;
    conv.appdata_ptr = &ctx;

    ret = pam_start(service_name, user, &conv, &pamh);
    if (ret != PAM_SUCCESS) return -1;

    ret = pam_setcred(pamh, PAM_ESTABLISH_CRED);
    if (ret != PAM_SUCCESS) {
        log_msg(1, "pam_setcred failed: %s", pam_strerror(pamh, ret));
    }

    pam_end(pamh, ret);
    return (ret == PAM_SUCCESS) ? 0 : -1;
}
