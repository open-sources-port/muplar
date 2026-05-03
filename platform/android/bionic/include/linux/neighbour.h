#pragma once

#ifdef __APPLE__

#include <linux/types.h>
#include <stdint.h>

/* Dummy definitions to allow Linux code that includes linux/neighbour.h to compile */

struct neighbour {
    int dummy;  /* placeholder */
};

struct ndmsg {
    __u8    ndm_family;
    __u8    ndm_pad1;
    __u16   ndm_pad2;
    __s32   ndm_ifindex;
    __u16   ndm_state;
    __u8    ndm_flags;
    __u8    ndm_type;
};

/* Neighbor/ARP flags */
#define NUD_PERMANENT   0
#define NUD_REACHABLE   0
#define NUD_STALE       0
#define NUD_DELAY       0
#define NUD_FAILED      0
#define NUD_NOARP       0
#define NUD_INCOMPLETE  0

/* Netlink message types (for neighbor/ARP) */
#define RTM_NEWNEIGH    0
#define RTM_DELNEIGH    0
#define RTM_GETNEIGH    0

struct nda_cacheinfo {
    uint32_t ndm_confirmed;
    uint32_t ndm_used;
    uint32_t ndm_updated;
    uint32_t ndm_refcnt;
};

#define NDA_DST        1
#define NDA_LLADDR     2
#define NDA_CACHEINFO  3
#define NDA_PROBES     4
#define NDA_VLAN       5
#define NDA_PORT       6
#define NDA_VNI        7
#define NDA_IFINDEX    8

#endif /* __APPLE__ */
