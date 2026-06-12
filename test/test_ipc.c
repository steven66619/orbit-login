#include "test_runner.h"
#include <sys/socket.h>
#include <signal.h>

static int test_ipc_send_recv_roundtrip(void) {
    TEST("ipc_send and ipc_recv roundtrip via socketpair") {
        int sv[2];
        ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, sv), 0, "socketpair");

        const char *test_data = "hello world";
        uint32_t len = strlen(test_data) + 1;

        ASSERT_EQ(ipc_send(sv[0], IPC_PING, test_data, len), 0, "send");

        ipc_header_t hdr;
        char buf[256];
        ASSERT_EQ(ipc_recv(sv[1], &hdr, buf, sizeof(buf)), 0, "recv");
        ASSERT_EQ(hdr.type, IPC_PING, "type preserved");
        ASSERT_EQ(hdr.payload_len, len, "payload length preserved");
        ASSERT_STR_EQ(buf, test_data, "payload data preserved");

        close(sv[0]);
        close(sv[1]);
    }
    END_TEST;
    return tests_failed;
}

static int test_ipc_send_recv_empty_payload(void) {
    TEST("ipc_send and ipc_recv with empty payload") {
        int sv[2];
        ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, sv), 0, "socketpair");

        ASSERT_EQ(ipc_send(sv[0], IPC_PING, NULL, 0), 0, "send empty");

        ipc_header_t hdr;
        ASSERT_EQ(ipc_recv(sv[1], &hdr, NULL, 0), 0, "recv empty");
        ASSERT_EQ(hdr.type, IPC_PING, "type preserved");
        ASSERT_EQ(hdr.payload_len, 0, "zero payload length");

        close(sv[0]);
        close(sv[1]);
    }
    END_TEST;
    return tests_failed;
}

static int test_ipc_send_recv_large_payload(void) {
    TEST("ipc_send and ipc_recv with large payload") {
        int sv[2];
        ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, sv), 0, "socketpair");

        char send_buf[8192];
        for (int i = 0; i < 8192; i++) send_buf[i] = (char)(i & 0xFF);

        ASSERT_EQ(ipc_send(sv[0], IPC_SESSION_LIST, send_buf, 8192), 0, "send large");

        ipc_header_t hdr;
        char recv_buf[8192];
        ASSERT_EQ(ipc_recv(sv[1], &hdr, recv_buf, sizeof(recv_buf)), 0, "recv large");
        ASSERT_EQ(hdr.type, IPC_SESSION_LIST, "type preserved");
        ASSERT_EQ(hdr.payload_len, 8192, "payload length preserved");
        ASSERT_EQ(memcmp(send_buf, recv_buf, 8192), 0, "payload data preserved");

        close(sv[0]);
        close(sv[1]);
    }
    END_TEST;
    return tests_failed;
}

static int test_ipc_recv_overflow(void) {
    TEST("ipc_recv rejects payload larger than max") {
        int sv[2];
        ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, sv), 0, "socketpair");

        char data[256];
        memset(data, 'A', 256);
        ASSERT_EQ(ipc_send(sv[0], IPC_PING, data, 200), 0, "send 200 bytes");

        ipc_header_t hdr;
        char small_buf[100];
        ASSERT_EQ(ipc_recv(sv[1], &hdr, small_buf, 50), -1,
                  "recv fails when max_payload < payload_len");

        close(sv[0]);
        close(sv[1]);
    }
    END_TEST;
    return tests_failed;
}

static int test_ipc_send_failure(void) {
    TEST("ipc_send returns -1 on closed fd") {
        struct sigaction sa, old;
        memset(&sa, 0, sizeof(sa));
        sa.sa_handler = SIG_IGN;
        sigaction(SIGPIPE, &sa, &old);

        int sv[2];
        ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, sv), 0, "socketpair");
        close(sv[1]);
        ASSERT_EQ(ipc_send(sv[0], IPC_PING, "data", 5), -1, "send on closed fd fails");
        close(sv[0]);

        sigaction(SIGPIPE, &old, NULL);
    }
    END_TEST;
    return tests_failed;
}

static int test_ipc_recv_failure(void) {
    TEST("ipc_recv returns -1 on closed fd") {
        int sv[2];
        ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, sv), 0, "socketpair");
        close(sv[1]);
        ipc_header_t hdr;
        ASSERT_EQ(ipc_recv(sv[0], &hdr, NULL, 0), -1, "recv on closed fd fails");
        close(sv[0]);
    }
    END_TEST;
    return tests_failed;
}

static int test_ipc_header_values(void) {
    TEST("ipc_send sets header fields correctly") {
        int sv[2];
        ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, sv), 0, "socketpair");

        ASSERT_EQ(ipc_send(sv[0], IPC_AUTH_RES, NULL, 0), 0, "send");

        ipc_header_t hdr;
        ASSERT_EQ(ipc_recv(sv[1], &hdr, NULL, 0), 0, "recv");
        ASSERT_EQ(hdr.type, IPC_AUTH_RES, "type IPC_AUTH_RES");
        ASSERT_EQ(hdr.payload_len, 0, "zero payload");

        close(sv[0]);
        close(sv[1]);
    }
    END_TEST;
    return tests_failed;
}
