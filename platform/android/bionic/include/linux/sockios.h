#ifndef _LINUX_SOCKIOS_H
#define _LINUX_SOCKIOS_H

#include <sys/ioctl.h>
#include <sys/sockio.h>
#include <net/if.h>

/* Linux-only ioctls not present on macOS */

#ifndef SIOCGIFHWADDR
/* macOS does not support Linux-style HWADDR ioctl */
#define SIOCGIFHWADDR 0x8927   /* Dummy, Linux value */
#endif

#ifndef SIOCSIFHWADDR
#define SIOCSIFHWADDR 0x8924   /* Dummy, Linux value */
#endif

#ifndef SIOCGIFINDEX
/* macOS has it in <net/if.h> */
#define SIOCGIFINDEX _IOWR('i', 42, struct ifreq)
#endif

/* Multicast operations (Linux values only for compatibility) */
#ifndef SIOCADDMULTI
#define SIOCADDMULTI  _IOW('i', 49, struct ifreq)
#endif

#ifndef SIOCDELMULTI
#define SIOCDELMULTI  _IOW('i', 50, struct ifreq)
#endif

#endif
