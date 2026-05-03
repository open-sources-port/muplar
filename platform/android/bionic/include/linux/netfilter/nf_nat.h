#ifndef NF_NAT_H_MAC
#define NF_NAT_H_MAC

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Protocol constants (like Linux)
#define IPPROTO_TCP 6
#define IPPROTO_UDP 17

// NAT range structure (similar to Linux)
struct nf_nat_range {
    char iface[64];           // Interface name, e.g., "en0"
    char src_cidr[64];        // Source subnet, e.g., "192.168.1.0/24"
    char dst_ip[64];          // Optional destination IP, e.g., "8.8.8.8"
    unsigned short min_src_port;
    unsigned short max_src_port;
    unsigned short min_dst_port;
    unsigned short max_dst_port;
    int proto;                // IPPROTO_TCP or IPPROTO_UDP
};

// nf_conn stub (Linux compatibility)
struct nf_conn {
    int dummy;
};

// Build port specification string for pfctl
static inline void build_port_spec(char *buf, size_t size,
                                   unsigned short min_port,
                                   unsigned short max_port) {
    if (min_port == 0 && max_port == 0) {
        buf[0] = '\0'; // no port
    } else if (min_port == max_port) {
        snprintf(buf, size, " port %u", min_port);
    } else {
        snprintf(buf, size, " port %u:%u", min_port, max_port);
    }
}

// Adds a NAT rule via pfctl
static inline int nf_nat_setup_info(struct nf_conn *ct, struct nf_nat_range *range, int proto_unused) {
    (void)ct;

    char src_port_spec[64] = {0};
    char dst_port_spec[64] = {0};

    build_port_spec(src_port_spec, sizeof(src_port_spec),
                    range->min_src_port, range->max_src_port);

    build_port_spec(dst_port_spec, sizeof(dst_port_spec),
                    range->min_dst_port, range->max_dst_port);

    char cmd[1024];
    if (range->dst_ip[0]) {
        // Destination NAT (redirect)
        snprintf(cmd, sizeof(cmd),
                 "echo 'rdr on %s proto %s from %s%s to any%s -> %s' | sudo pfctl -f -",
                 range->iface,
                 (range->proto == IPPROTO_UDP) ? "udp" : "tcp",
                 range->src_cidr,
                 src_port_spec,
                 dst_port_spec,
                 range->dst_ip);
    } else {
        // Source NAT (masquerade)
        snprintf(cmd, sizeof(cmd),
                 "echo 'nat on %s proto %s from %s%s to any -> (%s)' | sudo pfctl -f -",
                 range->iface,
                 (range->proto == IPPROTO_UDP) ? "udp" : "tcp",
                 range->src_cidr,
                 src_port_spec,
                 range->iface);
    }

    int ret = system(cmd);
    if (ret != 0) {
        fprintf(stderr, "Failed to apply NAT rule: %s\n", cmd);
        return -1;
    }

    return 0;
}

// Linux API compatibility
static inline int nf_nat_setup_info_permanently(struct nf_conn *ct, struct nf_nat_range *range, int proto) {
    return nf_nat_setup_info(ct, range, proto);
}

#endif // NF_NAT_H_MAC
