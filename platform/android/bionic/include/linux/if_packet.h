/* compat/if_packet.h
 * Minimal linux/if_packet.h compatibility shim for macOS (compile-time).
 *
 * Provides:
 *  - AF_PACKET, SOCKADDR_LL (sockaddr_ll) stub
 *  - PACKET_* constants that are sometimes tested at compile time
 *
 * Note: This is a compile-time shim only. For runtime packet I/O on macOS,
 * use BPF (/dev/bpfN). See bpf_socket.h / bpf_socket.c for helpers.
 */

#pragma once

#ifdef __APPLE__

/* common BSD headers */
#include <sys/types.h>
#include <sys/socket.h>
#include <net/if.h>
#include <stdint.h>
#include <string.h>

/* Provide AF_PACKET so code that uses AF_PACKET compiles.
 * Value chosen is arbitrary (not used by macOS sockets). */
#ifndef AF_PACKET
#define AF_PACKET 17
#endif

/* Linux sockaddr_ll stub (only fields commonly used) */
struct sockaddr_ll {
    unsigned short sll_family;   /* Always AF_PACKET (stub) */
    unsigned short sll_protocol; /* EtherType in network byte order */
    int            sll_ifindex;  /* Interface index */
    unsigned short sll_hatype;   /* ARPHRD_* */
    unsigned char  sll_pkttype;  /* PACKET_* */
    unsigned char  sll_halen;    /* length of address (e.g. 6) */
    unsigned char  sll_addr[8];  /* physical layer address (first sll_halen bytes) */
};

/* PACKET types (compile-time constants) */
#define PACKET_HOST        0   /* To us */
#define PACKET_BROADCAST   1   /* Broadcast */
#define PACKET_MULTICAST   2   /* Multicast */
#define PACKET_OTHERHOST   3   /* To someone else */
#define PACKET_OUTGOING    4   /* Outgoing packet */
#define PACKET_LOOPBACK    5   /* From loopback device */
#define PACKET_FASTROUTE   6

/* PACKET socket types (stubs) */
#define SOCK_PACKET        10  /* historical */
#define PACKET_HOST        0
#define PACKET_BROADCAST   1
#define PACKET_MULTICAST   2

/* packet() / sockaddr_ll helpers (optional macros) */
#ifndef htons
#include <arpa/inet.h>
#endif

/* For convenience: convert ifname -> sockaddr_ll initializer */
static inline void compat_sockaddr_ll_from_ifidx(struct sockaddr_ll *s, int ifindex, uint16_t proto, const unsigned char *hwaddr, unsigned char hwlen) {
    if (!s) return;
    memset(s, 0, sizeof(*s));
    s->sll_family = AF_PACKET;
    s->sll_ifindex = ifindex;
    s->sll_protocol = proto; /* assume network byte order already */
    s->sll_halen = hwlen;
    if (hwaddr && hwlen <= 8) memcpy(s->sll_addr, hwaddr, hwlen);
}

#else
/* On non-macOS platforms include the real header */
#include_next <linux/if_packet.h>
#endif /* __APPLE__ */
