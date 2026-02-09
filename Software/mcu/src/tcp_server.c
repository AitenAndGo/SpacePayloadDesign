#include "tcp_server.h"
#include "lwip/tcp.h"
#include <string.h>

#define TCP_PORT TCP_SERVER_PORT

static struct tcp_pcb *server_pcb;

uint8_t checksum(const uint8_t *data, size_t len) {
    uint8_t cs = 0;
    for (size_t i = 0; i < len; i++)
        cs ^= data[i];
    return cs;
}

static err_t tcp_recv_cb(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err) {
    if (!p) {
        tcp_close(tpcb);
        return ERR_OK;
    }

    if (p->len < 6) {
        pbuf_free(p);
        return ERR_OK; // incomplete frame
    }

    uint8_t *buf = (uint8_t *)p->payload;

    if (buf[0] == 'T' && buf[1] == 'C') {
        uint8_t cs = checksum(buf, 5);

        if (cs == buf[5]) {
            command = buf[2];
            param1  = buf[3];
            param2  = buf[4];
        } else {
            printf("Checksum error\n");
        }
    }

    pbuf_free(p);
    return ERR_OK;
}

static err_t tcp_accept_cb(void *arg, struct tcp_pcb *newpcb, err_t err) {
    printf("Client connected\n");
    tcp_recv(newpcb, tcp_recv_cb);
    return ERR_OK;
}

void send_telemetry(struct tcp_pcb *pcb,
                    uint8_t status,
                    uint16_t data,
                    uint32_t timestamp) {

    uint8_t frame[10];

    frame[0] = 'T';
    frame[1] = 'M';
    frame[2] = status;

    frame[3] = (data >> 8) & 0xFF;
    frame[4] = data & 0xFF;

    frame[5] = (timestamp >> 24) & 0xFF;
    frame[6] = (timestamp >> 16) & 0xFF;
    frame[7] = (timestamp >> 8) & 0xFF;
    frame[8] = timestamp & 0xFF;

    frame[9] = checksum(frame, 9);

    tcp_write(pcb, frame, sizeof(frame), TCP_WRITE_FLAG_COPY);
}


void start_tcp_server(void) {
    server_pcb = tcp_new_ip_type(IPADDR_TYPE_ANY);
    if (!server_pcb) {
        printf("Failed to create PCB\n");
        return;
    }

    tcp_bind(server_pcb, IP_ANY_TYPE, TCP_PORT);
    server_pcb = tcp_listen(server_pcb);

    tcp_accept(server_pcb, tcp_accept_cb);

    printf("TCP server listening on port %d\n", TCP_PORT);
}
