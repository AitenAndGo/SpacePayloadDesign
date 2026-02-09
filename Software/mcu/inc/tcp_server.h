#ifndef TCP_SERVER_H
#define TCP_SERVER_H

#include <stdint.h>
#include "lwip/tcp.h"

#define TCP_SERVER_PORT 4242

// Use -1 to indicate no new command pending
#define CMD_NONE -1

extern volatile int command;
extern volatile int param1;
extern volatile int param2;
extern struct tcp_pcb *server_pcb; // Expose PCB to send telemetry from main

void start_tcp_server(void);
void send_telemetry(struct tcp_pcb *pcb, uint8_t status, uint16_t data, uint32_t timestamp);

#endif /* TCP_SERVER_H */