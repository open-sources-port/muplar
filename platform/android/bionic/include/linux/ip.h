#ifndef _LINUX_IP_H
#define _LINUX_IP_H

#include <netinet/ip.h>   // macOS struct ip

#define NF_INET_PRE_ROUTING 0
#define NF_INET_LOCAL_OUT   1
#define NF_INET_POST_ROUTING 2
#define NF_IP_PRI_NAT_DST   0
#define NF_IP_PRI_NAT_SRC   0

/* Linux-compatible typedef for struct iphdr */

struct iphdr {
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    unsigned char ihl:4,
                  version:4;
#else
    unsigned char version:4,
                  ihl:4;
#endif
    uint8_t  tos;
    uint16_t tot_len;
    uint16_t id;
    uint16_t frag_off;
    uint8_t  ttl;
    uint8_t  protocol;
    uint16_t check;
    uint32_t saddr;
    uint32_t daddr;
};

#endif
