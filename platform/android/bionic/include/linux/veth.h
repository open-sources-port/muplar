#ifndef _LINUX_VETH_H
#define _LINUX_VETH_H

/* macOS has no veth devices, provide a stub */

#define VETH_INFO_MAX 1

struct veth_info {
    int ifindex_peer;
};

/* Dummy ioctl value to satisfy builds — unused on macOS */
#define SIOCGIFVETHINFO 0x8940

#endif
