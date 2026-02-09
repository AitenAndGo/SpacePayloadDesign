#ifndef TCP_SERVER_H
#define TCP_SERVER_H

/* =========================
 * TCP Server Configuration
 * ========================= */

/* TCP listening port */
#define TCP_SERVER_PORT 4242

extern volatile int command;
extern volatile int param1;
extern volatile int param2;

/* =========================
 * Public API
 * ========================= */

/**
 * @brief Initialize and start the TCP server.
 *
 * This function creates a TCP listening socket on the Pico 2W,
 * binds it to the configured port, and registers receive callbacks.
 *
 * It shall be called after Wi-Fi initialization and connection.
 */
void start_tcp_server(void);

// void set_ap_ip(void);

#endif /* TCP_SERVER_H */
