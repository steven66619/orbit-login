#include "orbit.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>

int ipc_send(int fd, ipc_msg_type_t type, const void *data, uint32_t len) {
    ipc_header_t hdr;
    hdr.type = type;
    hdr.seq = 0;
    hdr.payload_len = len;

    if (write(fd, &hdr, sizeof(hdr)) != sizeof(hdr)) return -1;
    if (len > 0 && data) {
        if (write(fd, data, len) != (ssize_t)len) return -1;
    }
    return 0;
}

int ipc_recv(int fd, ipc_header_t *hdr, void *payload, uint32_t max_payload) {
    memset(hdr, 0, sizeof(*hdr));
    if (read(fd, hdr, sizeof(*hdr)) != sizeof(*hdr)) return -1;
    if (hdr->payload_len > 0) {
        if (hdr->payload_len > max_payload) return -1;
        if (read(fd, payload, hdr->payload_len) != (ssize_t)hdr->payload_len) return -1;
    }
    return 0;
}
