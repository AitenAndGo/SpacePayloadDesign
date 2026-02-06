#ifndef LWIPOPTS_H
#define LWIPOPTS_H

/* ---------- System options ---------- */

#define NO_SYS                     1
#define LWIP_SOCKET                0
#define LWIP_NETCONN               0

/* ---------- Memory options ---------- */

#define MEM_ALIGNMENT              4
#define MEM_SIZE                   (16 * 1024)

#define MEMP_NUM_PBUF              16
#define MEMP_NUM_TCP_PCB           5
#define MEMP_NUM_TCP_SEG           16

/* ---------- TCP options ---------- */

#define LWIP_TCP                   1
#define TCP_MSS                    1460
#define TCP_SND_BUF                (4 * TCP_MSS)
#define TCP_WND                    (4 * TCP_MSS)

#define LWIP_CALLBACK_API          1

/* ---------- Debug (optional) ---------- */

#define LWIP_STATS                 0
#define LWIP_DEBUG                 0

#endif /* LWIPOPTS_H */
