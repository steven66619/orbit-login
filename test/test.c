/* SPDX-License-Identifier: GPL-3.0-only
 *
 * orbit-login - display manager for Bedrock Linux
 * Copyright (C) 2025  Steven Ende
 */

#include "test_runner.h"

#include <fcntl.h>
#include <sys/socket.h>
#include <unistd.h>

#define _GNU_SOURCE

/* Include source files under test */
#include "../src/config.c"
#include "../src/ipc_client.c"

/* Undefine main so we can define our own */
#ifdef main
#undef main
#endif

/* Override log_msg to be silent during tests */
void log_msg(int level, const char *fmt, ...) {
    (void)level;
    (void)fmt;
}

/* Include test cases */
#include "test_config.c"
#include "test_ipc.c"

int main(void) {
    printf("orbit-login test suite\n");
    printf("========================================\n");

    printf("\n--- config tests ---\n");
    test_defaults();
    test_parse_empty();
    test_parse_basic_conf();
    test_parse_comments_and_sections();
    test_parse_invalid_values();

    printf("\n--- ipc protocol tests ---\n");
    test_ipc_send_recv_roundtrip();
    test_ipc_send_recv_empty_payload();
    test_ipc_send_recv_large_payload();
    test_ipc_recv_overflow();
    test_ipc_send_failure();
    test_ipc_recv_failure();
    test_ipc_header_values();

    return test_summary();
}
