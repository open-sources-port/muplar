#pragma once

/*
 * Linux ARP Hardware Types (subset)
 * Source: /usr/include/linux/if_arp.h
 */

#define ARPHRD_NETROM     0
#define ARPHRD_ETHER      1       /* Ethernet 10/100/1000 */
#define ARPHRD_EETHER     2
#define ARPHRD_AX25       3
#define ARPHRD_PRONET     4
#define ARPHRD_CHAOS      5
#define ARPHRD_IEEE802    6
#define ARPHRD_ARCNET     7
#define ARPHRD_APPLETLK   8
#define ARPHRD_DLCI       15
#define ARPHRD_ATM        19
#define ARPHRD_LOOPBACK   772
#define ARPHRD_IEEE80211  801     /* Wi-Fi */
#define ARPHRD_NONE       0xFFFFFFFF
