#pragma once

#if defined(__APPLE__)

#include <sys_compat/muplar_errno.h>
#include <fcntl.h>
#include <sys_compat/muplar_limits.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/*
 * Simulated Linux file_handle for macOS.
 *
 * This stores a canonical absolute path inside f_handle[].
 * handle_type is always 0.
 */
struct file_handle {
    unsigned int handle_bytes;      // Length of stored path (including '\0')
    int handle_type;                // Always 0 on macOS
    unsigned char f_handle[PATH_MAX]; // Stores path
};


/*
 * Simulated name_to_handle_at() using realpath().
 *
 * Behavior:
 *   - Converts path to canonical absolute path
 *   - Writes it into f_handle[]
 *   - Sets handle_bytes
 *   - mount_id is set to 0 (macOS has no Linux mount IDs)
 *
 * Returns:
 *   0              success
 *  -1, errno set   on failure
 */
static inline int macos_name_to_handle_at(
    int dirfd,
    const char *path,
    struct file_handle *handle,
    int *mount_id,
    int flags)
{
    (void)dirfd;
    (void)flags;

    char resolved[PATH_MAX];

    if (!path || !handle) {
        errno = EINVAL;
        return -1;
    }

    if (!realpath(path, resolved)) {
        return -1;  // errno propagated
    }

    size_t len = strlen(resolved) + 1;

    if (len > sizeof(handle->f_handle)) {
        errno = ENAMETOOLONG;
        return -1;
    }

    memcpy(handle->f_handle, resolved, len);
    handle->handle_bytes = (unsigned int)len;
    handle->handle_type  = 0;

    if (mount_id)
        *mount_id = 0;

    return 0;
}


/*
 * Simulated open_by_handle_at() using stored absolute path.
 *
 * Returns:
 *   fd >= 0        success
 *  -1, errno set   failure
 */
static inline int macos_open_by_handle_at(
    int mount_fd,
    struct file_handle *handle,
    int flags)
{
    (void)mount_fd;

    if (!handle || handle->handle_bytes == 0) {
        errno = EINVAL;
        return -1;
    }

    const char *path = (const char *)handle->f_handle;
    return open(path, flags);
}


/* Provide Linux-like names so systemd compiles without changes */
#define name_to_handle_at     macos_name_to_handle_at
#define open_by_handle_at     macos_open_by_handle_at

#endif // __APPLE__
