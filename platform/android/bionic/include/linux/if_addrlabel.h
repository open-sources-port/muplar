/* Dummy stub for macOS – Linux-only addrlabel API */

#ifndef _LINUX_IF_ADDRLABEL_H
#define _LINUX_IF_ADDRLABEL_H

// Full struct (Linux-like size)
struct ifaddrlblmsg {
    __u8 ifal_family;
    __u8 __ifal_reserved;
    __u8 ifal_prefixlen;
    __u8 ifal_flags;
    __u32 ifal_index;
    __u32 ifal_seq;
};

// Alias so you can also use the Linux-style name
typedef struct ifaddrlblmsg ifaddrlabelmsg;


enum {
    IFAL_ADDRESS = 1,
    IFAL_LABEL = 2,
    IFAL_MAX = 3,
};

#endif
