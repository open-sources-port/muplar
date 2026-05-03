#pragma once

#include <stdint.h>
#include <sys/socket.h> // for struct sockaddr
#include <net/if.h>     // macOS interface flags

/* Interface name sizes (keep for Linux compatibility) */
#define IFNAMSIZ_LINUX    16
#define IFALIASZ          256
#define ALTIFNAMSIZ       128

/* Linux protocol constants (no conflicts) */
#define IF_IFACE_V35   0x1000
#define IF_IFACE_V24   0x1001
#define IF_IFACE_X21   0x1002
#define IF_IFACE_T1    0x1003
#define IF_IFACE_E1    0x1004
#define IF_IFACE_SYNC_SERIAL 0x1005
#define IF_IFACE_X21D  0x1006

#define IF_PROTO_HDLC      0x2000
#define IF_PROTO_PPP       0x2001
#define IF_PROTO_CISCO     0x2002
#define IF_PROTO_FR        0x2003
#define IF_PROTO_FR_ADD_PVC 0x2004
#define IF_PROTO_FR_DEL_PVC 0x2005
#define IF_PROTO_X25       0x2006
#define IF_PROTO_HDLC_ETH  0x2007
#define IF_PROTO_FR_ADD_ETH_PVC 0x2008
#define IF_PROTO_FR_DEL_ETH_PVC 0x2009
#define IF_PROTO_FR_PVC    0x200A
#define IF_PROTO_FR_ETH_PVC 0x200B
#define IF_PROTO_RAW       0x200C

#ifndef IFF_LOWER_UP
#define IFF_LOWER_UP 0
#endif

#ifndef IFF_DORMANT
#define IFF_DORMANT 0
#endif

/* RFC 2863 operational status */
enum {
    IF_OPER_UNKNOWN,
    IF_OPER_NOTPRESENT,
    IF_OPER_DOWN,
    IF_OPER_LOWERLAYERDOWN,
    IF_OPER_TESTING,
    IF_OPER_DORMANT,
    IF_OPER_UP
};

/* Link modes */
enum {
    IF_LINK_MODE_DEFAULT,
    IF_LINK_MODE_DORMANT,
    IF_LINK_MODE_TESTING
};

/* Minimal simulated ifreq using macOS types */
typedef struct {
    char ifrn_name[IFNAMSIZ];  /* interface name */
    union {
        struct sockaddr ifru_addr;
        struct sockaddr ifru_dstaddr;
        struct sockaddr ifru_broadaddr;
        struct sockaddr ifru_netmask;
        short ifru_flags;
        int ifru_ivalue;
        void *ifru_data;
    } ifr_ifru;
} ifreq;

#define ifr_flags   ifr_ifru.ifru_flags
#define ifr_data    ifr_ifru.ifru_data

/* Minimal ifconf */
typedef struct {
    int ifc_len;
    union {
        char *ifcu_buf;
        ifreq *ifcu_req;
    } ifc_ifcu;
} ifconf;

#define ifc_buf ifc_ifcu.ifcu_buf
#define ifc_req ifc_ifcu.ifcu_req
