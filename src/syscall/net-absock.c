/* Abstract AF_UNIX emulation helpers
 *
 * Copyright 2026 elfuse contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <pthread.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>

#include "syscall/net.h"
#include "syscall/net-absock.h"

#define ABSOCK_MAX_ENTRIES 64
#define ABSOCK_MAX_NAME 107

typedef struct {
    int guest_fd;
    uint8_t name[ABSOCK_MAX_NAME];
    uint32_t name_len;
    char fs_path[104];
    bool active;
} absock_entry_t;

static pthread_mutex_t absock_lock = PTHREAD_MUTEX_INITIALIZER;
static absock_entry_t absock_table[ABSOCK_MAX_ENTRIES];
static char absock_dir[128];
static bool absock_dir_created;
static _Atomic uint64_t absock_namespace_id;
static _Atomic uint32_t absock_autobind_counter;

static int absock_ensure_dir_locked(void)
{
    uint64_t namespace_id = atomic_load(&absock_namespace_id);

    if (absock_dir_created)
        return 0;

    if (namespace_id == 0) {
        namespace_id = (uint64_t) getpid();
        atomic_store(&absock_namespace_id, namespace_id);
    }
    snprintf(absock_dir, sizeof(absock_dir), "/tmp/elfuse-absock-%llu",
             (unsigned long long) namespace_id);
    if (mkdir(absock_dir, 0700) < 0 && errno != EEXIST)
        return -1;
    absock_dir_created = true;
    return 0;
}

uint64_t absock_get_namespace_id(void)
{
    uint64_t namespace_id = atomic_load(&absock_namespace_id);
    if (namespace_id == 0)
        return (uint64_t) getpid();
    return namespace_id;
}

void absock_set_namespace_id(uint64_t namespace_id)
{
    if (namespace_id == 0)
        namespace_id = (uint64_t) getpid();
    atomic_store(&absock_namespace_id, namespace_id);
}

static void absock_encode_name(const uint8_t *name,
                               uint32_t len,
                               char *out,
                               size_t out_sz)
{
    size_t dir_len = strlen(absock_dir);
    size_t max_hex = out_sz - dir_len - 2;
    size_t hex_needed = (size_t) len * 2;

    size_t pos = (size_t) snprintf(out, out_sz, "%s/", absock_dir);
    if (hex_needed <= max_hex) {
        for (uint32_t i = 0; i < len && pos + 2 < out_sz; i++)
            pos += (size_t) snprintf(out + pos, out_sz - pos, "%02x", name[i]);
    } else {
        uint32_t prefix_bytes = 20;
        if (prefix_bytes > len)
            prefix_bytes = len;
        for (uint32_t i = 0; i < prefix_bytes && pos + 2 < out_sz; i++)
            pos += (size_t) snprintf(out + pos, out_sz - pos, "%02x", name[i]);
        uint32_t h = 0x811c9dc5;
        for (uint32_t i = 0; i < len; i++) {
            h ^= name[i];
            h *= 0x01000193;
        }
        snprintf(out + pos, out_sz - pos, "%08x", h);
    }
}

static const char *absock_lookup_locked(const uint8_t *name, uint32_t name_len)
{
    for (int i = 0; i < ABSOCK_MAX_ENTRIES; i++) {
        if (absock_table[i].active && absock_table[i].name_len == name_len &&
            !memcmp(absock_table[i].name, name, name_len)) {
            return absock_table[i].fs_path;
        }
    }
    return NULL;
}

static int absock_register_locked(int guest_fd,
                                  const uint8_t *name,
                                  uint32_t name_len,
                                  char *fs_path_out,
                                  size_t fs_path_sz)
{
    for (int i = 0; i < ABSOCK_MAX_ENTRIES; i++) {
        if (!absock_table[i].active) {
            absock_table[i].guest_fd = guest_fd;
            absock_table[i].name_len = name_len;
            memcpy(absock_table[i].name, name, name_len);
            absock_encode_name(name, name_len, absock_table[i].fs_path,
                               sizeof(absock_table[i].fs_path));
            if (fs_path_out)
                snprintf(fs_path_out, fs_path_sz, "%s",
                         absock_table[i].fs_path);
            return i;
        }
    }
    return -1;
}

void absock_unregister_fd(int guest_fd)
{
    pthread_mutex_lock(&absock_lock);
    for (int i = 0; i < ABSOCK_MAX_ENTRIES; i++) {
        if (absock_table[i].active && absock_table[i].guest_fd == guest_fd) {
            unlink(absock_table[i].fs_path);
            absock_table[i].active = false;
        }
    }
    pthread_mutex_unlock(&absock_lock);
}

int absock_reverse_lookup(const char *fs_path,
                          uint8_t *out_name,
                          uint32_t *out_len)
{
    pthread_mutex_lock(&absock_lock);
    for (int i = 0; i < ABSOCK_MAX_ENTRIES; i++) {
        if (absock_table[i].active &&
            !strcmp(absock_table[i].fs_path, fs_path)) {
            *out_len = absock_table[i].name_len;
            memcpy(out_name, absock_table[i].name, absock_table[i].name_len);
            pthread_mutex_unlock(&absock_lock);
            return 1;
        }
    }
    pthread_mutex_unlock(&absock_lock);
    return 0;
}

int absock_is_abstract_unix(const uint8_t *linux_sa, uint32_t addrlen)
{
    if (addrlen < 4)
        return 0;
    uint16_t fam;
    memcpy(&fam, linux_sa, 2);
    if (fam != LINUX_AF_UNIX)
        return 0;
    return linux_sa[2] == '\0';
}

static int absock_build_sun(const char *fs_path,
                            struct sockaddr_storage *mac_sa)
{
    struct sockaddr_un *sun = (struct sockaddr_un *) mac_sa;
    memset(sun, 0, sizeof(*sun));
    sun->sun_len = sizeof(*sun);
    sun->sun_family = AF_UNIX;
    size_t path_len = strlen(fs_path);
    if (path_len >= sizeof(sun->sun_path))
        return -1;
    memcpy(sun->sun_path, fs_path, path_len + 1);
    return (int) (offsetof(struct sockaddr_un, sun_path) + path_len + 1);
}

int absock_rewrite_connect(const uint8_t *linux_sa,
                           uint32_t addrlen,
                           struct sockaddr_storage *mac_sa)
{
    const uint8_t *abs_name = linux_sa + 3;
    uint32_t abs_len = addrlen - 3;
    if (abs_len > ABSOCK_MAX_NAME)
        abs_len = ABSOCK_MAX_NAME;

    pthread_mutex_lock(&absock_lock);
    const char *fs_path = absock_lookup_locked(abs_name, abs_len);
    char path_buf[104];
    if (!fs_path) {
        if (absock_ensure_dir_locked() < 0) {
            pthread_mutex_unlock(&absock_lock);
            return -1;
        }
        absock_encode_name(abs_name, abs_len, path_buf, sizeof(path_buf));
        fs_path = path_buf;
    }
    int ret = absock_build_sun(fs_path, mac_sa);
    pthread_mutex_unlock(&absock_lock);
    return ret;
}

int absock_bind_prepare(const uint8_t *linux_sa,
                        uint32_t addrlen,
                        struct sockaddr_storage *mac_sa,
                        int guest_fd,
                        int *out_len)
{
    uint8_t name_buf[ABSOCK_MAX_NAME];
    const uint8_t *abs_name;
    uint32_t abs_len;

    if (addrlen <= 3) {
        uint32_t seq = absock_autobind_counter++;
        abs_len = (uint32_t) snprintf((char *) name_buf, sizeof(name_buf),
                                      "%05x", seq);
        abs_name = name_buf;
    } else {
        abs_name = linux_sa + 3;
        abs_len = addrlen - 3;
        if (abs_len > ABSOCK_MAX_NAME)
            return -1;
    }

    pthread_mutex_lock(&absock_lock);
    if (absock_ensure_dir_locked() < 0) {
        pthread_mutex_unlock(&absock_lock);
        return -1;
    }
    if (absock_lookup_locked(abs_name, abs_len)) {
        pthread_mutex_unlock(&absock_lock);
        return -2;
    }

    char fs_path[104];
    int idx = absock_register_locked(guest_fd, abs_name, abs_len, fs_path,
                                     sizeof(fs_path));
    pthread_mutex_unlock(&absock_lock);
    if (idx < 0)
        return -1;

    *out_len = absock_build_sun(fs_path, mac_sa);
    if (*out_len < 0)
        return -1;
    return idx;
}

void absock_bind_commit(int idx)
{
    pthread_mutex_lock(&absock_lock);
    absock_table[idx].active = true;
    pthread_mutex_unlock(&absock_lock);
}

void absock_bind_rollback(int idx)
{
    pthread_mutex_lock(&absock_lock);
    absock_table[idx].name_len = 0;
    absock_table[idx].guest_fd = -1;
    pthread_mutex_unlock(&absock_lock);
}

static void absock_cleanup(void)
{
    for (int i = 0; i < ABSOCK_MAX_ENTRIES; i++) {
        if (absock_table[i].active)
            unlink(absock_table[i].fs_path);
    }
    if (absock_dir_created)
        rmdir(absock_dir);
}

void absock_init_cleanup(void)
{
    static int registered;
    if (!registered) {
        atexit(absock_cleanup);
        registered = 1;
    }
}
