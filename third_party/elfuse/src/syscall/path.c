/* Shared guest/host path handling
 *
 * Copyright 2026 elfuse contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <limits.h>
#include <sys/stat.h>

#include "utils.h"

#include "syscall/abi.h"
#include "syscall/path.h"
#include "syscall/proc.h"

#ifndef MAXSYMLINKS
#define MAXSYMLINKS 40
#endif

#define PROC_PATH_COMPONENTS_MAX (LINUX_PATH_MAX / 2)

/* True when path equals prefix exactly, or extends it with '/'. Avoids the
 * surprise where "/sys/devices/system/cpufoo" would match a bare strncmp on
 * "/sys/devices/system/cpu" and pull an unrelated path through the intercept
 * layer.
 */
static bool path_prefix_match(const char *path, const char *prefix, size_t plen)
{
    if (strncmp(path, prefix, plen) != 0)
        return false;
    return path[plen] == '\0' || path[plen] == '/';
}

#define SYSFS_CPU_PREFIX "/sys/devices/system/cpu"

bool path_might_use_open_intercept(const char *path)
{
    if (!path || path[0] != '/')
        return false;

    if (!strncmp(path, "/proc", 5))
        return true;
    if (!strncmp(path, "/dev", 4))
        return true;
    if (path_prefix_match(path, SYSFS_CPU_PREFIX, sizeof(SYSFS_CPU_PREFIX) - 1))
        return true;
    if (!strcmp(path, "/etc/mtab") || !strcmp(path, "/etc/passwd") ||
        !strcmp(path, "/etc/group"))
        return true;
    if (!strcmp(path, "/var/run/utmp") || !strcmp(path, "/run/utmp"))
        return true;

    return false;
}

bool path_might_use_stat_intercept(const char *path)
{
    if (!path || path[0] != '/')
        return false;

    if (!strncmp(path, "/proc", 5))
        return true;
    if (!strncmp(path, "/dev/shm", 8))
        return true;
    if (path_prefix_match(path, SYSFS_CPU_PREFIX, sizeof(SYSFS_CPU_PREFIX) - 1))
        return true;

    return false;
}

int path_check_intercept_access(const struct stat *st, int mode, int flags)
{
    if ((mode & ~(F_OK | R_OK | W_OK | X_OK)) != 0) {
        errno = EINVAL;
        return -1;
    }
    if (mode == F_OK)
        return 0;

    mode_t granted = 0;
    uint32_t uid =
        (flags & LINUX_AT_EACCESS) ? proc_get_euid() : proc_get_uid();
    uint32_t gid =
        (flags & LINUX_AT_EACCESS) ? proc_get_egid() : proc_get_gid();

    if (uid == 0) {
        /* CAP_DAC_OVERRIDE: root reads and writes any file regardless of mode
         * bits. Execute still requires at least one x-bit set so non-executable
         * files cannot be run as root. Matches Linux generic_permission() in
         * fs/namei.c.
         */
        granted |= R_OK | W_OK;
        if (st->st_mode & (S_IXUSR | S_IXGRP | S_IXOTH))
            granted |= X_OK;
    } else {
        mode_t bits;
        if (uid == st->st_uid)
            bits = (st->st_mode >> 6) & 7;
        else if (gid == st->st_gid)
            bits = (st->st_mode >> 3) & 7;
        else
            bits = st->st_mode & 7;

        if (bits & 4)
            granted |= R_OK;
        if (bits & 2)
            granted |= W_OK;
        if (bits & 1)
            granted |= X_OK;
    }

    if ((mode & granted) == mode)
        return 0;

    errno = EACCES;
    return -1;
}

static bool path_next_component(const char **pathp,
                                const char **comp,
                                size_t *len)
{
    const char *p = *pathp;

    while (*p == '/')
        p++;
    if (*p == '\0') {
        *pathp = p;
        return false;
    }

    *comp = p;
    while (*p != '\0' && *p != '/')
        p++;
    *len = (size_t) (p - *comp);
    *pathp = p;
    return true;
}

static bool path_component_is_dot(const char *comp, size_t len)
{
    return len == 1 && comp[0] == '.';
}

const char *path_resolve_sysroot_path(const char *path, char *buf, size_t bufsz)
{
    return proc_resolve_sysroot_path(path, buf, bufsz);
}

const char *path_resolve_sysroot_nofollow_path(const char *path,
                                               char *buf,
                                               size_t bufsz)
{
    return proc_resolve_sysroot_nofollow_path(path, buf, bufsz);
}

const char *path_resolve_sysroot_create_path(const char *path,
                                             char *buf,
                                             size_t bufsz)
{
    return proc_resolve_sysroot_create_path(path, buf, bufsz);
}

int sys_path_has_symlink(guest_fd_t dirfd, const char *path)
{
    if (!path || path[0] == '\0')
        return 0;

    host_fd_t base_fd = -1;
    bool owned_base_fd = false;
    const char *scan = path;
    char sysroot_buf[LINUX_PATH_MAX];

    if (path[0] == '/') {
        const char *host_path = path_resolve_sysroot_nofollow_path(
            path, sysroot_buf, sizeof(sysroot_buf));
        if (!host_path) {
            errno = ENAMETOOLONG;
            return -1;
        }
        base_fd = open("/", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
        if (base_fd < 0)
            return -1;
        owned_base_fd = true;
        scan = host_path;
    } else {
        host_fd_ref_t dir_ref;
        if (host_dirfd_ref_open(dirfd, &dir_ref) < 0) {
            errno = EBADF;
            return -1;
        }
        if (dir_ref.fd == AT_FDCWD) {
            base_fd = open(".", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
            if (base_fd < 0)
                return -1;
            owned_base_fd = true;
        } else {
            base_fd = dir_ref.fd;
            owned_base_fd = dir_ref.owned;
        }
    }

    host_fd_t current_fd = base_fd;
    bool owned_current_fd = owned_base_fd;
    const char *comp;
    size_t len;
    int rc = 0;
    int walk_count = 0;

    while (path_next_component(&scan, &comp, &len)) {
        if (++walk_count > MAXSYMLINKS) {
            errno = ELOOP;
            rc = -1;
            goto out;
        }
        if (path_component_is_dot(comp, len))
            continue;

        char name[NAME_MAX + 1];
        if (len > NAME_MAX) {
            errno = ENAMETOOLONG;
            rc = -1;
            goto out;
        }
        memcpy(name, comp, len);
        name[len] = '\0';

        struct stat st;
        if (fstatat(current_fd, name, &st, AT_SYMLINK_NOFOLLOW) < 0) {
            rc = -1;
            goto out;
        }
        if (S_ISLNK(st.st_mode)) {
            errno = ELOOP;
            rc = -1;
            goto out;
        }

        const char *rest = scan;
        while (*rest == '/')
            rest++;
        if (*rest == '\0')
            break;
        if (!S_ISDIR(st.st_mode)) {
            errno = ENOTDIR;
            rc = -1;
            goto out;
        }

        host_fd_t next_fd =
            openat(current_fd, name, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
        if (next_fd < 0) {
            rc = -1;
            goto out;
        }
        if (owned_current_fd)
            close(current_fd);
        current_fd = next_fd;
        owned_current_fd = true;
    }

out:
    if (owned_current_fd && current_fd >= 0)
        close(current_fd);
    return rc;
}

static int proc_push_component(char *out,
                               size_t outsz,
                               size_t marks[],
                               size_t *depth,
                               const char *comp,
                               size_t len)
{
    size_t cur = strlen(out);
    size_t write_pos = cur;

    if (cur == 1 && out[0] == '/') {
        if (cur + len >= outsz)
            return -1;
        marks[*depth] = cur;
        memcpy(out + cur, comp, len);
        out[cur + len] = '\0';
        (*depth)++;
        return 0;
    }

    if (cur + 1 + len >= outsz)
        return -1;

    marks[*depth] = write_pos;
    out[write_pos] = '/';
    memcpy(out + write_pos + 1, comp, len);
    out[write_pos + 1 + len] = '\0';
    (*depth)++;
    return 0;
}

static int proc_apply_components(const char *path,
                                 char *out,
                                 size_t outsz,
                                 size_t marks[],
                                 size_t marks_cap,
                                 size_t *depth)
{
    const char *seg = path;
    while (*seg) {
        while (*seg == '/')
            seg++;
        if (*seg == '\0')
            break;

        const char *end = seg;
        while (*end != '\0' && *end != '/')
            end++;

        size_t len = (size_t) (end - seg);
        if (len == 1 && seg[0] == '.') {
            seg = end;
            continue;
        }
        if (len == 2 && seg[0] == '.' && seg[1] == '.') {
            if (*depth > 0) {
                *depth -= 1;
                out[marks[*depth]] = '\0';
            }
            seg = end;
            continue;
        }
        if (*depth >= marks_cap) {
            errno = ENAMETOOLONG;
            return -1;
        }
        if (proc_push_component(out, outsz, marks, depth, seg, len) < 0)
            return -1;
        seg = end;
    }
    return 0;
}

static int proc_seed_absolute_path(const char *path,
                                   char *out,
                                   size_t outsz,
                                   size_t marks[],
                                   size_t marks_cap,
                                   size_t *depth)
{
    *depth = 0;
    str_copy_trunc(out, "/", outsz);
    return proc_apply_components(path, out, outsz, marks, marks_cap, depth);
}

int resolve_proc_dirfd_path(guest_fd_t dirfd,
                            const char *path,
                            char *out,
                            size_t outsz)
{
    if (dirfd == LINUX_AT_FDCWD || !path || path[0] == '/')
        return 0;

    fd_entry_t snap;
    if (!fd_snapshot(dirfd, &snap) || snap.type != FD_DIR ||
        snap.proc_path[0] == '\0')
        return 0;

    size_t marks[PROC_PATH_COMPONENTS_MAX];
    size_t depth;
    if (proc_seed_absolute_path(snap.proc_path, out, outsz, marks,
                                ARRAY_SIZE(marks), &depth) < 0 ||
        proc_apply_components(path, out, outsz, marks, ARRAY_SIZE(marks),
                              &depth) < 0) {
        errno = ENAMETOOLONG;
        return -1;
    }

    return 1;
}

static int resolve_proc_cwd_path(const char *path, char *out, size_t outsz)
{
    if (!path || path[0] == '\0' || path[0] == '/')
        return 0;

    proc_cwd_view_t view;
    if (proc_acquire_cwd_view(&view) < 0)
        return 0;

    int rc = 0;
    if (!strncmp(view.path, "/proc", 5)) {
        size_t marks[PROC_PATH_COMPONENTS_MAX];
        size_t depth;
        if (proc_seed_absolute_path(view.path, out, outsz, marks,
                                    ARRAY_SIZE(marks), &depth) < 0 ||
            proc_apply_components(path, out, outsz, marks, ARRAY_SIZE(marks),
                                  &depth) < 0) {
            errno = ENAMETOOLONG;
            rc = -1;
        } else {
            rc = 1;
        }
    }

    proc_release_cwd_view(&view);
    return rc;
}

int resolve_proc_at_path(guest_fd_t dirfd,
                         const char *path,
                         char *out,
                         size_t outsz)
{
    if (!path || path[0] == '\0' || path[0] == '/')
        return 0;

    int rc = resolve_proc_dirfd_path(dirfd, path, out, outsz);
    if (rc != 0 || dirfd != LINUX_AT_FDCWD)
        return rc;

    return resolve_proc_cwd_path(path, out, outsz);
}

int path_openat2_stays_beneath(const char *path, bool clamp_at_root)
{
    int depth = 0;
    const char *p = path;

    while (*p) {
        while (*p == '/')
            p++;
        if (*p == '\0')
            break;

        const char *start = p;
        while (*p && *p != '/')
            p++;
        size_t len = (size_t) (p - start);

        if (len == 1 && start[0] == '.')
            continue;
        if (len == 2 && start[0] == '.' && start[1] == '.') {
            if (depth == 0) {
                if (!clamp_at_root)
                    return 0;
            } else {
                depth--;
            }
            continue;
        }
        depth++;
    }

    return 1;
}

int path_openat2_normalize_in_root(const char *path, char *out, size_t outsz)
{
    size_t depth = 0;
    size_t marks[LINUX_PATH_MAX / 2];
    size_t out_len = 0;
    const char *p = path;

    if (outsz == 0)
        return -1;
    out[0] = '\0';

    while (*p) {
        while (*p == '/')
            p++;
        if (*p == '\0')
            break;

        const char *start = p;
        while (*p && *p != '/')
            p++;
        size_t len = (size_t) (p - start);

        if (len == 1 && start[0] == '.')
            continue;
        if (len == 2 && start[0] == '.' && start[1] == '.') {
            if (depth > 0) {
                out_len = marks[depth - 1];
                out[out_len] = '\0';
                depth--;
            }
            continue;
        }

        if (depth >= (LINUX_PATH_MAX / 2))
            return -1;

        marks[depth] = out_len;
        if (out_len != 0) {
            if (out_len + 1 >= outsz)
                return -1;
            out[out_len++] = '/';
        }
        if (out_len + len >= outsz)
            return -1;
        memcpy(out + out_len, start, len);
        out_len += len;
        out[out_len] = '\0';
        depth++;
    }

    if (out_len == 0) {
        if (outsz < 2)
            return -1;
        out[0] = '.';
        out[1] = '\0';
    }

    return 0;
}

int path_openat2_is_proc_magiclink(guest_fd_t dirfd, const char *path)
{
    if (!path)
        return 0;

    if (!strncmp(path, "/proc/self/fd/", 14))
        return 1;
    if (path[0] == '/')
        return 0;

    if (dirfd == LINUX_AT_FDCWD) {
        proc_cwd_view_t view;
        if (proc_acquire_cwd_view(&view) == 0) {
            int is_magiclink =
                (!strcmp(view.path, "/proc") &&
                 !strncmp(path, "self/fd/", 8)) ||
                (!strcmp(view.path, "/proc/self") && !strncmp(path, "fd/", 3));
            proc_release_cwd_view(&view);
            if (is_magiclink)
                return 1;
        }
    }

    fd_entry_t snap;
    if (!fd_snapshot(dirfd, &snap) || snap.proc_path[0] == '\0')
        return 0;

    if (!strcmp(snap.proc_path, "/proc"))
        return !strncmp(path, "self/fd/", 8);
    if (!strcmp(snap.proc_path, "/proc/self"))
        return !strncmp(path, "fd/", 3);

    return 0;
}

static int path_openat2_dirfd_host_path(guest_fd_t dirfd,
                                        char *out,
                                        size_t outsz)
{
    if (dirfd == LINUX_AT_FDCWD) {
        if (!getcwd(out, outsz))
            return -1;
        return 0;
    }

    host_fd_ref_t dir_ref;
    if (host_dirfd_ref_open(dirfd, &dir_ref) < 0) {
        errno = EBADF;
        return -1;
    }
    int rc = fcntl(dir_ref.fd, F_GETPATH, out);
    host_fd_ref_close(&dir_ref);
    return rc < 0 ? -1 : 0;
}

int path_openat2_resolved_within_root(guest_fd_t dirfd,
                                      const char *path,
                                      uint64_t oflags,
                                      bool in_root)
{
    char root_path[LINUX_PATH_MAX], joined[LINUX_PATH_MAX];
    char real_root[LINUX_PATH_MAX], real_path[LINUX_PATH_MAX];
    const char *rel = path;
    char normalized[LINUX_PATH_MAX];

    if (path_openat2_dirfd_host_path(dirfd, root_path, sizeof(root_path)) < 0)
        return -1;
    if (!realpath(root_path, real_root))
        return -1;

    if (in_root) {
        if (path_openat2_normalize_in_root(path, normalized,
                                           sizeof(normalized)) < 0) {
            errno = ENAMETOOLONG;
            return -1;
        }
        rel = normalized;
    } else if (path[0] == '/') {
        errno = EXDEV;
        return -1;
    }

    if (*rel == '\0')
        rel = ".";

    if (snprintf(joined, sizeof(joined), "%s/%s", real_root, rel) >=
        (int) sizeof(joined)) {
        errno = ENAMETOOLONG;
        return -1;
    }

    bool follow_final = true;
    if (oflags & LINUX_O_NOFOLLOW)
        follow_final = false;
    if (oflags & LINUX_O_CREAT)
        follow_final = false;

    if (follow_final) {
        if (!realpath(joined, real_path))
            return -1;
    } else {
        char parent[LINUX_PATH_MAX];
        char *slash;

        str_copy_trunc(parent, joined, sizeof(parent));
        slash = strrchr(parent, '/');
        if (!slash)
            return -1;
        if (slash == parent) {
            parent[1] = '\0';
        } else {
            *slash = '\0';
        }
        if (!realpath(parent, real_path))
            return -1;

        size_t parent_len = strlen(real_path);
        const char *tail = (slash == parent) ? joined + 1 : slash + 1;
        if (snprintf(real_path + parent_len, sizeof(real_path) - parent_len,
                     "/%s", tail) >= (int) (sizeof(real_path) - parent_len)) {
            errno = ENAMETOOLONG;
            return -1;
        }
    }

    size_t root_len = strlen(real_root);
    if (strncmp(real_path, real_root, root_len) != 0 ||
        (real_path[root_len] != '\0' && real_path[root_len] != '/')) {
        errno = EXDEV;
        return -1;
    }

    return 0;
}
