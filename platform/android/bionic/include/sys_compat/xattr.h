#pragma once

#ifdef __APPLE__
#include <sys/xattr.h>
#include <string.h>

/* Map Linux-style flags to macOS options */
static inline int setxattr_portable(const char *path, const char *name,
                                    const void *value, size_t size, int flags) {
    int options = 0;
    if (flags & XATTR_CREATE)  options |= XATTR_CREATE;
    if (flags & XATTR_REPLACE) options |= XATTR_REPLACE;
    return setxattr(path, name, value, size, 0, options);
}

static inline ssize_t getxattr_portable(const char *path, const char *name,
                                        void *value, size_t size) {
    return getxattr(path, name, value, size, 0, 0);
}

static inline int removexattr_portable(const char *path, const char *name) {
    return removexattr(path, name, 0);
}

/* lsetxattr/lremovexattr equivalents (do not follow symlinks) */
static inline int lsetxattr(const char *path, const char *name,
                            const void *value, size_t size, int flags) {
    int options = 0;
    if (flags & XATTR_CREATE)  options |= XATTR_CREATE;
    if (flags & XATTR_REPLACE) options |= XATTR_REPLACE;
    options |= XATTR_NOFOLLOW;
    return setxattr(path, name, value, size, 0, options);
}

static inline int lremovexattr(const char *path, const char *name) {
    return removexattr(path, name, XATTR_NOFOLLOW);
}

#else
#include <sys/xattr.h> /* Linux has setxattr/lsetxattr already */
#endif
