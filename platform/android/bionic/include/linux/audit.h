#ifndef COMPAT_LINUX_AUDIT_H
#define COMPAT_LINUX_AUDIT_H

#include <stddef.h>  // size_t
#include <stdint.h>  // uint32_t, uint64_t etc
#include <unistd.h>  // ssize_t, gid_t, uid_t
#include <sys_compat/muplar_errno.h>   // errno constants
#include <bsm/audit.h>

#define AUDIT_FEATURE_ARG_LEN 1024
#define AUDIT_STATUS_ENABLED 1
#define AUDIT_GET_FEATURE 0xBEEF

// typedef int32_t au_event_t; // placeholder type
typedef int audit_fd_t;     // fake fd type

// ------------------------------------------------------------
// Message types (subset used by systemd)
// ------------------------------------------------------------
#define AUDIT_GET          1000    /* Get status */
#define AUDIT_SET          1001    /* Set status */
#define AUDIT_LIST         1002    /* List rules */
#define AUDIT_ADD          1003    /* Add rule */
#define AUDIT_DEL          1004    /* Delete rule */
#define AUDIT_SIGNAL_INFO  1012    /* Get info about sender */

// ------------------------------------------------------------
// Arch tokens (for identifying syscall ABI)
// ------------------------------------------------------------
#define AUDIT_ARCH_X86_64  0xC000003E
#define AUDIT_ARCH_I386    0x40000003
#define AUDIT_ARCH_ARM64   0xC00000B7

// ------------------------------------------------------------
// Service (daemon)
// ------------------------------------------------------------
#ifndef AUDIT_SERVICE_START
#define AUDIT_SERVICE_START 1130 /* Service (daemon) start */
#endif

#ifndef AUDIT_SERVICE_STOP
#define AUDIT_SERVICE_STOP 1131 /* Service (daemon) stop */
#endif

#ifndef MAX_AUDIT_MESSAGE_LENGTH
#define MAX_AUDIT_MESSAGE_LENGTH 8970
#endif

#ifndef AUDIT_NLGRP_MAX
#define AUDIT_NLGRP_READLOG 1
#endif

// ------------------------------------------------------------
// Status structure
// ------------------------------------------------------------
struct audit_status {
    uint32_t mask;
    uint32_t enabled;
    uint32_t failure;
    uint32_t pid;
    uint32_t rate_limit;
    uint32_t backlog_limit;
    uint32_t lost;
    uint32_t backlog;
};

// ------------------------------------------------------------
// Simple rule data structure (empty stub)
// ------------------------------------------------------------
struct audit_rule_data {
    uint32_t flags;
    uint32_t action;
    uint32_t field_count;
    uint32_t mask[64];
    uint32_t fields[64];
    uint32_t values[64];
    uint32_t fieldflags[64];
    uint32_t buflen;
    char buf[0];
};

static inline int audit_open(void) {
    // No-op on macOS
    return -1;
}

static inline int audit_send(int fd, void *msg, size_t size) {
    // No-op
    (void)fd;
    (void)msg;
    (void)size;
    return -1;
}

static inline int audit_log_user_message(int fd, au_event_t event,
                                         const char *msg, uint32_t len,
                                         uint32_t *sessionid) {
    (void)fd;
    (void)event;
    (void)msg;
    (void)len;
    (void)sessionid;
    return -1;
}

#endif // COMPAT_LINUX_AUDIT_H
