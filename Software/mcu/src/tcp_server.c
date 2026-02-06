#include "tcp_server.h"
#include "lwip/tcp.h"
#include <string.h>

#define TCP_PORT TCP_SERVER_PORT

static struct tcp_pcb *server_pcb;

static err_t tcp_recv_cb(void *arg, struct tcp_pcb *tpcb,
    struct pbuf *p, err_t err) {
if (!p) {
tcp_close(tpcb);
return ERR_OK;
}

printf("Received %d bytes\n", p->len);

// Send reply
const char *msg = "Hello from Pico 2 W TCP server!\n";
tcp_write(tpcb, msg, strlen(msg), TCP_WRITE_FLAG_COPY);

pbuf_free(p);
return ERR_OK;
}

static err_t tcp_accept_cb(void *arg, struct tcp_pcb *newpcb, err_t err) {
    printf("Client connected\n");
    tcp_recv(newpcb, tcp_recv_cb);
    return ERR_OK;
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
