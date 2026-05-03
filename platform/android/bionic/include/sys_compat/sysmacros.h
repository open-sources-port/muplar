/* compat/sys/sysmacros.h
 *
 * Minimal compatibility header for major/minor/makedev on non-Linux systems.
 *
 * Put this file in a compat include directory and add -I/path/to/compat to your
 * build system. If your system already provides these macros, this header
 * simply defers to the system definitions.
 */

#ifndef COMPAT_SYS_SYSMACROS_H
#define COMPAT_SYS_SYSMACROS_H

#include <sys/types.h>
#include <stdint.h>

/* If the system already has these, do nothing. */
#ifdef major
/* system provides major/minor/makedev */
#else

/* Fallback encoding:
 *   dev_t layout: [major:8][minor:8] (16-bit device number)
 *
 * This is a conservative fallback. If you need the Linux 12/20 or other
 * encodings, replace masks/shifts below accordingly.
 */

#ifndef __u32
typedef uint32_t __u32;
#endif

static inline unsigned int compat_major(dev_t dev) {
    /* extract top 8 bits */
    return (unsigned int)((((__u32) dev) >> 8) & 0xffu);
}

static inline unsigned int compat_minor(dev_t dev) {
    /* extract low 8 bits */
    return (unsigned int)(((unsigned int)((__u32) dev)) & 0xffu);
}

static inline dev_t compat_makedev(unsigned int major, unsigned int minor) {
    return (dev_t)((((__u32)(major & 0xffu)) << 8) | ( (__u32)(minor & 0xffu) ));
}

/* Provide macro names expected by code */
#define major(dev) compat_major(dev)
#define minor(dev) compat_minor(dev)
#define makedev(maj,min) compat_makedev((maj),(min))

#endif /* major */

#endif /* COMPAT_SYS_SYSMACROS_H */
