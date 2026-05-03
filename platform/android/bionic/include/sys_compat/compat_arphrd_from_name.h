#ifndef COMPAT_ARPHRD_FROM_NAME_H
#define COMPAT_ARPHRD_FROM_NAME_H

#ifdef __APPLE__

/*
 * Stub header for macOS/BSD builds.
 * Linux provides <linux/arphrd-from-name.h> to map ARPHRD_* to names.
 * Here we provide a minimal dummy implementation.
 */

#include <net/if_arp.h>   /* for ARPHRD_* constants if needed */

inline int arphrd_from_name(const char *name) {
    if (!name)
        return -1;

    if (strcmp(name, "ether") == 0)
        return 1;  // ARPHRD_ETHER
    if (strcmp(name, "loopback") == 0)
        return 772; // ARPHRD_LOOPBACK
    // add more mappings as needed

    return -1; // unknown
}

#endif

#endif /* COMPAT_ARPHRD_FROM_NAME_H */
