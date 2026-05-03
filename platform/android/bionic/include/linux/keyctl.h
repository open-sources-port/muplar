#pragma once

#ifdef __APPLE__

#include <sys_compat/muplar_errno.h>
#include <stdarg.h>
#include <stdint.h>

// Linux key types and commands (stubbed)
typedef int key_serial_t;

#define KEYCTL_GET_KEYRING_ID   0
#define KEYCTL_JOIN_SESSION_KEYRING 1
#define KEYCTL_UPDATE 2
#define KEYCTL_REVOKE 3
#define KEYCTL_CHOWN 4
#define KEYCTL_SETPERM 5
#define KEYCTL_DESCRIBE 6
#define KEYCTL_CLEAR 7

// Stub keyctl function: does nothing and returns -1
static inline int keyctl(int cmd, ...) {
    (void)cmd;
    va_list ap;
    va_start(ap, cmd);
    // Consume extra arguments
    va_end(ap);
    errno = ENOSYS; // function not implemented
    return -1;
}

// Optional: stub for key types if referenced
typedef struct {
    int dummy;
} key_type_t;

static inline key_serial_t request_key(
        const char *type,
        const char *description,
        const char *callout_info,
        key_serial_t dest_keyring) {

    (void) type;
    (void) description;
    (void) callout_info;
    (void) dest_keyring;

    errno = ENOSYS;
    return -1;
}

static inline key_serial_t add_key(
        const char *type,
        const char *description,
        const void *payload,
        size_t plen,
        key_serial_t keyring) {

    (void) type;
    (void) description;
    (void) payload;
    (void) plen;
    (void) keyring;

    errno = ENOSYS;
    return -1;
}

#endif // __APPLE__
