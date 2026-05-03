#pragma once

#ifdef __APPLE__

/* IPPROTO_* constants from Linux */
#define IPPROTO_IP      0
#define IPPROTO_ICMP    1
#define IPPROTO_IGMP    2
#define IPPROTO_TCP     6
#define IPPROTO_UDP     17
#define IPPROTO_RAW     255

#define IP_MULTICAST_ALL		49
#define IP_UNICAST_IF			50
#define IP_LOCAL_PORT_RANGE		51
#define IP_PROTOCOL			52

/* Include macOS socket headers for sockaddr_in, etc. */
#include <netinet/in.h>
#include <sys/socket.h>

/* Dummy enum for code that expects enum IPPROTO_* */
#ifndef IPPROTO_IP_ENUM
#define IPPROTO_IP_ENUM
enum {
    LINUX_IPPROTO_IP      = 0,
    LINUX_IPPROTO_ICMP    = 1,
    LINUX_IPPROTO_IGMP    = 2,
    LINUX_IPPROTO_TCP     = 6,
    LINUX_IPPROTO_UDP     = 17,
    LINUX_IPPROTO_RAW     = 255,
};
#endif

/* Other macros that may be needed */
#ifndef INADDR_ANY
#define INADDR_ANY       ((in_addr_t)0x00000000)
#endif
#ifndef INADDR_BROADCAST
#define INADDR_BROADCAST ((in_addr_t)0xffffffff)
#endif
#ifndef INADDR_LOOPBACK
#define INADDR_LOOPBACK  ((in_addr_t)0x7f000001)
#endif

#else /* Linux */
#include <linux/in.h>
#endif
