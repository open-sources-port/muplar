#ifndef COMPAT_IF_INFINIBAND_H
#define COMPAT_IF_INFINIBAND_H

#ifdef __APPLE__

/*
 * Stub definitions to allow Linux code including <linux/if_infiniband.h>
 * to compile on macOS. These do not provide actual Infiniband support.
 */

#define INFINIBAND_ALEN      20   /* Address length (20 bytes) */
#define INFINIBAND_LINK_LAYER 0   /* Dummy link layer type */

struct sockaddr_ib {
    unsigned short  sib_family;      /* Address family */
    unsigned short  sib_pkey;        /* Partition key */
    unsigned int    sib_flowinfo;    /* Flow information */
    unsigned char   sib_addr[INFINIBAND_ALEN]; /* Infiniband address */
    unsigned int    sib_scope_id;    /* Scope ID */
};

#endif

#endif /* COMPAT_IF_INFINIBAND_H */
