#ifndef COMPAT_ARPHRD_TO_NAME_H
#define COMPAT_ARPHRD_TO_NAME_H

#ifdef __APPLE__

/*
 * Stub header for macOS/BSD builds.
 * Linux provides <linux/arphrd-to-name.h> for mapping ARPHRD_* to names.
 * This provides a minimal dummy implementation so code can compile.
 */

#include <net/if_arp.h>  /* for ARPHRD_* constants */

inline const char* arphrd_to_name(int arphrd) {
    switch (arphrd) {
        case 1:    return "ether";      // ARPHRD_ETHER
        case 772:  return "loopback";   // ARPHRD_LOOPBACK
        // add more mappings as needed
        default:   return "unknown";
    }
}
#endif

#endif /* COMPAT_ARPHRD_TO_NAME_H */
