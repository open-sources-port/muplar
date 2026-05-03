#ifndef NETFILTER_IPV4_H_MAC
#define NETFILTER_IPV4_H_MAC

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pcap.h>
#include <arpa/inet.h>
#include <netinet/ip.h>
#include <netinet/tcp.h>
#include <netinet/udp.h>
#include <pthread.h>

// Linux hook constants
#define NF_IP_PRE_ROUTING   0
#define NF_IP_POST_ROUTING  1
#define NF_IP_LOCAL_IN      2
#define NF_IP_LOCAL_OUT     3

typedef int (*nf_hookfn)(unsigned char *packet, size_t len, void *userdata);

// Linux-style hook registration struct
struct nf_hook_ops {
    int hooknum;            // NF_IP_PRE_ROUTING, etc.
    nf_hookfn hook;         // callback function
    void *userdata;         // optional user data
    char iface[64];         // capture interface, e.g., "en0"
    pthread_t thread;       // internal capture thread
    int active;
};

// Internal function: packet capture thread
static void *nf_hook_capture_thread(void *arg) {
    struct nf_hook_ops *ops = (struct nf_hook_ops *)arg;
    char errbuf[PCAP_ERRBUF_SIZE];
    pcap_t *handle = pcap_open_live(ops->iface, 65536, 1, 1000, errbuf);
    if (!handle) {
        fprintf(stderr, "pcap_open_live failed: %s\n", errbuf);
        return NULL;
    }

    struct pcap_pkthdr *header;
    const unsigned char *data;
    int ret;

    while (ops->active) {
        ret = pcap_next_ex(handle, &header, &data);
        if (ret == 1 && header && data) {
            // Call user hook
            ops->hook((unsigned char *)data, header->len, ops->userdata);
        }
    }

    pcap_close(handle);
    return NULL;
}

// Register a hook
static inline int nf_register_hook(struct nf_hook_ops *ops) {
    if (!ops || !ops->hook || !ops->iface[0]) return -1;

    ops->active = 1;
    if (pthread_create(&ops->thread, NULL, nf_hook_capture_thread, ops) != 0) {
        perror("pthread_create");
        return -1;
    }
    return 0;
}

// Unregister a hook
static inline int nf_unregister_hook(struct nf_hook_ops *ops) {
    if (!ops) return -1;
    ops->active = 0;
    pthread_join(ops->thread, NULL);
    return 0;
}

#endif // NETFILTER_IPV4_H_MAC
