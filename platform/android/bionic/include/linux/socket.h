// compat/linux_socket.h (for macOS)
#pragma once

#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <sys/un.h>
#include <sys/ucred.h>
#include <fcntl.h>

// Linux -> macOS mappings

#define VMADDR_CID_ANY -1U

struct sockaddr_vm {
    sa_family_t     svm_family;
    unsigned short  svm_reserved1;
    unsigned int    svm_port;
    unsigned int    svm_cid;
    unsigned char   svm_zero[
        sizeof(struct sockaddr)
        - sizeof(sa_family_t)
        - sizeof(unsigned short)
        - sizeof(unsigned int)
        - sizeof(unsigned int)
    ];
};

#ifndef SOCK_NONBLOCK
#define SOCK_NONBLOCK O_NONBLOCK
#endif

#ifndef SOCK_CLOEXEC
#define SOCK_CLOEXEC FD_CLOEXEC
#endif

#ifndef AF_BRIDGE
#define AF_BRIDGE 7
#endif

// Linux AF_ numbers (optional)
#define AF_NETLINK  16     // stub, not functional
#define AF_PACKET   17     // stub, not functional

// Linux SO_ options (make them no-op if needed)
#ifndef SO_PEERCRED
#define SO_PEERCRED LOCAL_PEERCRED
#endif

// Missing features: BINDTODEVICE, PACKET sockets, NETLINK
static inline int setsockopt_noop(int fd, int level, int opt, const void *v, socklen_t l) {
    return 0; // always succeed
}

#ifndef SO_BINDTODEVICE
#define SO_BINDTODEVICE 25 
#endif

#ifndef AF_VSOCK
#define AF_VSOCK 40
#endif

#ifndef SO_REUSEPORT
#define SO_REUSEPORT 15
#endif

#ifndef SO_PEERGROUPS
#define SO_PEERGROUPS 59
#endif

#ifndef SO_BINDTOIFINDEX
#define SO_BINDTOIFINDEX 62
#endif

#ifndef SOL_NETLINK
#define SOL_NETLINK 270
#endif

#ifndef SOL_ALG
#define SOL_ALG 279
#endif

/* Not exposed yet. Defined in include/linux/socket.h. */
#ifndef SOL_SCTP
#define SOL_SCTP 132
#endif

/* Not exposed yet. Defined in include/linux/socket.h */
#ifndef SCM_SECURITY
#define SCM_SECURITY 0x03
#endif

/* netinet/in.h */
#ifndef IP_FREEBIND
#define IP_FREEBIND 15
#endif

#ifndef IP_TRANSPARENT
#define IP_TRANSPARENT 19
#endif

#ifndef IPV6_FREEBIND
#define IPV6_FREEBIND 78
#endif

#ifndef IP_RECVFRAGSIZE
#define IP_RECVFRAGSIZE 25
#endif

#ifndef IPV6_RECVFRAGSIZE
#define IPV6_RECVFRAGSIZE 77
#endif

/* linux/sockios.h */
#ifndef SIOCGSKNS
#define SIOCGSKNS 0x894C
#endif

static inline int accept4(int sockfd, struct sockaddr *addr, socklen_t *addrlen, int flags) {
    int fd = accept(sockfd, addr, addrlen);
    if (fd == -1) return -1;
    if (flags & SOCK_NONBLOCK) fcntl(fd, F_SETFL, O_NONBLOCK);
    if (flags & SOCK_CLOEXEC) fcntl(fd, F_SETFD, FD_CLOEXEC);
    return fd;
}