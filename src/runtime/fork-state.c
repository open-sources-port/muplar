/* Fork IPC state serialization
 *
 * Copyright 2026 elfuse contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

#include "utils.h"

#include "runtime/fork-state.h"

#include "debug/log.h"
#include "syscall/abi.h"
#include "syscall/internal.h"
#include "syscall/proc.h"

int fork_ipc_write_all(int fd, const void *buf, size_t len)
{
    const uint8_t *p = buf;
    while (len > 0) {
        ssize_t n = write(fd, p, len);
        if (n < 0) {
            if (errno == EINTR)
                continue;
            return -1;
        }
        p += n;
        len -= n;
    }
    return 0;
}

int fork_ipc_read_all(int fd, void *buf, size_t len)
{
    uint8_t *p = buf;
    while (len > 0) {
        ssize_t n = read(fd, p, len);
        if (n <= 0) {
            if (n < 0 && errno == EINTR)
                continue;
            return -1;
        }
        p += n;
        len -= n;
    }
    return 0;
}

int fork_ipc_send_fds(int sock, const int *fds, int count)
{
    if (count <= 0)
        return 0;

    char dummy = 'F';
    struct iovec iov = {.iov_base = &dummy, .iov_len = 1};
    size_t cmsg_size = CMSG_SPACE(count * sizeof(int));
    uint8_t *cmsg_buf = calloc(1, cmsg_size);
    if (!cmsg_buf)
        return -1;

    struct msghdr msg = {0};
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;
    msg.msg_control = cmsg_buf;
    msg.msg_controllen = cmsg_size;

    struct cmsghdr *cmsg = CMSG_FIRSTHDR(&msg);
    cmsg->cmsg_level = SOL_SOCKET;
    cmsg->cmsg_type = SCM_RIGHTS;
    cmsg->cmsg_len = CMSG_LEN(count * sizeof(int));
    memcpy(CMSG_DATA(cmsg), fds, count * sizeof(int));

    ssize_t ret = sendmsg(sock, &msg, 0);
    free(cmsg_buf);
    return ret < 0 ? -1 : 0;
}

int fork_ipc_recv_fds(int sock, int *fds, int max_count, int *out_count)
{
    char dummy;
    struct iovec iov = {.iov_base = &dummy, .iov_len = 1};
    size_t cmsg_size = CMSG_SPACE(max_count * sizeof(int));
    uint8_t *cmsg_buf = calloc(1, cmsg_size);
    if (!cmsg_buf)
        return -1;

    struct msghdr msg = {0};
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;
    msg.msg_control = cmsg_buf;
    msg.msg_controllen = cmsg_size;

    ssize_t ret = recvmsg(sock, &msg, 0);
    if (ret < 0) {
        free(cmsg_buf);
        return -1;
    }

    *out_count = 0;
    struct cmsghdr *cmsg = CMSG_FIRSTHDR(&msg);
    if (cmsg && cmsg->cmsg_level == SOL_SOCKET &&
        cmsg->cmsg_type == SCM_RIGHTS) {
        if (cmsg->cmsg_len < CMSG_LEN(0)) {
            free(cmsg_buf);
            return -1;
        }
        int n = (int) ((cmsg->cmsg_len - CMSG_LEN(0)) / sizeof(int));
        if (n > max_count)
            n = max_count;
        memcpy(fds, CMSG_DATA(cmsg), n * sizeof(int));
        *out_count = n;
    }

    free(cmsg_buf);
    return 0;
}

int fork_ipc_send_memory_regions(int ipc_sock, const guest_t *g, bool use_shm)
{
    if (use_shm) {
        uint32_t zero_regions = 0;
        return fork_ipc_write_all(ipc_sock, &zero_regions,
                                  sizeof(zero_regions));
    }

#define MAX_USED_REGIONS 16
    used_region_t used[MAX_USED_REGIONS];
    unsigned int shim_sz = proc_get_shim_size();
    pthread_mutex_lock(&mmap_lock);
    int nregions = guest_get_used_regions(g, shim_sz, used, MAX_USED_REGIONS);
    pthread_mutex_unlock(&mmap_lock);

    uint32_t num_regions = (uint32_t) nregions;
    if (fork_ipc_write_all(ipc_sock, &num_regions, sizeof(num_regions)) < 0)
        return -1;

    for (int i = 0; i < nregions; i++) {
        ipc_region_header_t rhdr = {
            .offset = used[i].offset,
            .size = used[i].size,
        };
        if (fork_ipc_write_all(ipc_sock, &rhdr, sizeof(rhdr)) < 0)
            return -1;

        uint8_t *src = (uint8_t *) g->host_base + used[i].offset;
        size_t remaining = used[i].size;
        while (remaining > 0) {
            size_t chunk = remaining > ((size_t) 1024 * 1024)
                               ? ((size_t) 1024 * 1024)
                               : remaining;
            if (fork_ipc_write_all(ipc_sock, src, chunk) < 0)
                return -1;
            src += chunk;
            remaining -= chunk;
        }
    }

    return 0;
}

int fork_ipc_recv_memory_regions(int ipc_fd, guest_t *g)
{
    uint32_t num_regions;
    if (fork_ipc_read_all(ipc_fd, &num_regions, sizeof(num_regions)) < 0) {
        log_error("fork-child: failed to read region count");
        return -1;
    }

    if (num_regions > 0)
        log_debug("fork-child: receiving %u memory regions", num_regions);

    for (uint32_t i = 0; i < num_regions; i++) {
        ipc_region_header_t rhdr;
        if (fork_ipc_read_all(ipc_fd, &rhdr, sizeof(rhdr)) < 0) {
            log_error("fork-child: failed to read region header");
            return -1;
        }

        if (rhdr.offset > g->guest_size ||
            rhdr.size > g->guest_size - rhdr.offset) {
            log_error("fork-child: region out of bounds");
            return -1;
        }

        uint8_t *dst = (uint8_t *) g->host_base + rhdr.offset;
        size_t remaining = rhdr.size;
        while (remaining > 0) {
            size_t chunk = remaining > ((size_t) 1024 * 1024)
                               ? ((size_t) 1024 * 1024)
                               : remaining;
            if (fork_ipc_read_all(ipc_fd, dst, chunk) < 0) {
                log_error("fork-child: failed to read region data");
                return -1;
            }
            dst += chunk;
            remaining -= chunk;
        }

        log_debug("fork-child: region %u: offset=0x%llx size=0x%llx", i,
                  (unsigned long long) rhdr.offset,
                  (unsigned long long) rhdr.size);
    }

    return 0;
}

int fork_ipc_send_fd_table(int ipc_sock)
{
    ipc_fd_entry_t fd_entries[FD_TABLE_SIZE];
    int host_fds_to_send[FD_TABLE_SIZE];
    bool host_fds_duped[FD_TABLE_SIZE];
    uint32_t num_fds = 0;

    pthread_mutex_lock(&fd_lock);
    for (int i = 0; i < FD_TABLE_SIZE; i++) {
        if (fd_table[i].type == FD_CLOSED)
            continue;

        int host_fd;
        bool was_duped = false;
        if (fd_table[i].type != FD_STDIO) {
            int duped = dup(fd_table[i].host_fd);
            if (duped < 0)
                continue;
            host_fd = duped;
            was_duped = true;
        } else {
            host_fd = fd_table[i].host_fd;
        }

        fd_entries[num_fds].guest_fd = i;
        fd_entries[num_fds].type = fd_table[i].type;
        fd_entries[num_fds].linux_flags = fd_table[i].linux_flags;
        fd_entries[num_fds].seals = fd_table[i].seals;
        memcpy(fd_entries[num_fds].proc_path, fd_table[i].proc_path,
               sizeof(fd_entries[num_fds].proc_path));
        host_fds_to_send[num_fds] = host_fd;
        host_fds_duped[num_fds] = was_duped;
        num_fds++;
    }
    pthread_mutex_unlock(&fd_lock);

    if (fork_ipc_write_all(ipc_sock, &num_fds, sizeof(num_fds)) < 0)
        goto fail;

    if (num_fds > 0) {
        if (fork_ipc_write_all(ipc_sock, fd_entries,
                               num_fds * sizeof(ipc_fd_entry_t)) < 0)
            goto fail;
        if (fork_ipc_send_fds(ipc_sock, host_fds_to_send, (int) num_fds) < 0) {
            log_error("clone: failed to send fds via SCM_RIGHTS");
            goto fail;
        }
    }

    for (uint32_t fi = 0; fi < num_fds; fi++)
        if (host_fds_duped[fi])
            close(host_fds_to_send[fi]);
    return 0;

fail:
    for (uint32_t fi = 0; fi < num_fds; fi++)
        if (host_fds_duped[fi])
            close(host_fds_to_send[fi]);
    return -1;
}

int fork_ipc_recv_fd_table(int ipc_fd, guest_t *g)
{
    (void) g;

    uint32_t num_fds;
    if (fork_ipc_read_all(ipc_fd, &num_fds, sizeof(num_fds)) < 0) {
        log_error("fork-child: failed to read fd count");
        return -1;
    }

    syscall_init();

    if (num_fds > FD_TABLE_SIZE) {
        log_error("fork-child: num_fds %u exceeds FD_TABLE_SIZE", num_fds);
        return -1;
    }

    if (num_fds == 0)
        return 0;

    ipc_fd_entry_t *fd_entries = calloc(num_fds, sizeof(ipc_fd_entry_t));
    if (!fd_entries)
        return -1;

    if (fork_ipc_read_all(ipc_fd, fd_entries,
                          num_fds * sizeof(ipc_fd_entry_t)) < 0) {
        free(fd_entries);
        return -1;
    }

    int *host_fds = calloc(num_fds, sizeof(int));
    if (!host_fds) {
        free(fd_entries);
        return -1;
    }

    int received_count = 0;
    if (fork_ipc_recv_fds(ipc_fd, host_fds, (int) num_fds, &received_count) <
        0) {
        log_error("fork-child: failed to receive fds");
        free(host_fds);
        free(fd_entries);
        return -1;
    }

    if (received_count != (int) num_fds) {
        log_error("fork-child: fd count mismatch: received %d, expected %u",
                  received_count, num_fds);
        for (int fi = 0; fi < received_count; fi++)
            close(host_fds[fi]);
        free(host_fds);
        free(fd_entries);
        return -1;
    }

    for (uint32_t i = 0; i < num_fds; i++) {
        int gfd = fd_entries[i].guest_fd;
        if (!RANGE_CHECK(gfd, 0, FD_TABLE_SIZE))
            continue;

        if (fd_entries[i].type == FD_STDIO) {
            close(host_fds[i]);
            fd_table[gfd].linux_flags = fd_entries[i].linux_flags;
            memcpy(fd_table[gfd].proc_path, fd_entries[i].proc_path,
                   sizeof(fd_table[gfd].proc_path));
            fd_table[gfd].seals = fd_entries[i].seals;
        } else {
            fd_alloc_at(gfd, fd_entries[i].type, host_fds[i]);
            fd_table[gfd].linux_flags = fd_entries[i].linux_flags;
            memcpy(fd_table[gfd].proc_path, fd_entries[i].proc_path,
                   sizeof(fd_table[gfd].proc_path));
            fd_table[gfd].seals = fd_entries[i].seals;

            if (fd_entries[i].type != FD_DIR)
                continue;
            int dir_fd = dup(host_fds[i]);
            if (dir_fd < 0) {
                log_error("fork-child: dup failed for DIR gfd %d: %s", gfd,
                          strerror(errno));
                continue;
            }
            DIR *dir = fdopendir(dir_fd);
            if (!dir) {
                close(dir_fd);
                log_error("fork-child: fdopendir failed for gfd %d", gfd);
                continue;
            }
            fd_table[gfd].dir = dir;
        }
    }

    free(host_fds);
    free(fd_entries);
    return 0;
}

static int fork_ipc_send_backing_fds(int ipc_sock,
                                     const guest_region_t *regions_snapshot,
                                     uint32_t num_guest_regions)
{
    int backing_fds[GUEST_MAX_REGIONS];
    uint32_t nbacking = 0;

    for (uint32_t i = 0; i < num_guest_regions; i++) {
        if (regions_snapshot[i].backing_fd >= 0)
            backing_fds[nbacking++] = regions_snapshot[i].backing_fd;
    }

    if (fork_ipc_write_all(ipc_sock, &nbacking, sizeof(nbacking)) < 0)
        return -1;
    if (nbacking == 0)
        return 0;

    char dummy = 'B';
    struct iovec iov = {.iov_base = &dummy, .iov_len = 1};
    size_t cmsg_sz = CMSG_SPACE(nbacking * sizeof(int));
    uint8_t *cmsg_buf = calloc(1, cmsg_sz);
    if (!cmsg_buf)
        return -1;

    struct msghdr msg = {
        .msg_iov = &iov,
        .msg_iovlen = 1,
        .msg_control = cmsg_buf,
        .msg_controllen = cmsg_sz,
    };
    struct cmsghdr *cmsg = CMSG_FIRSTHDR(&msg);
    cmsg->cmsg_level = SOL_SOCKET;
    cmsg->cmsg_type = SCM_RIGHTS;
    cmsg->cmsg_len = CMSG_LEN(nbacking * sizeof(int));
    memcpy(CMSG_DATA(cmsg), backing_fds, nbacking * sizeof(int));
    int ret = sendmsg(ipc_sock, &msg, 0);
    free(cmsg_buf);
    return ret < 0 ? -1 : 0;
}

int fork_ipc_send_process_state(int ipc_sock,
                                const guest_region_t *regions_snapshot,
                                uint32_t num_guest_regions)
{
    char cwd[LINUX_PATH_MAX] = {0};
    getcwd(cwd, sizeof(cwd));
    mode_t cur_umask = umask(0);
    umask(cur_umask);
    uint32_t umask_val = (uint32_t) cur_umask;
    if (fork_ipc_write_all(ipc_sock, cwd, sizeof(cwd)) < 0 ||
        fork_ipc_write_all(ipc_sock, &umask_val, sizeof(umask_val)) < 0)
        return -1;

    char sysroot_ipc[LINUX_PATH_MAX] = {0};
    const char *sr = proc_get_sysroot();
    if (sr)
        str_copy_trunc(sysroot_ipc, sr, sizeof(sysroot_ipc));
    if (fork_ipc_write_all(ipc_sock, sysroot_ipc, sizeof(sysroot_ipc)) < 0)
        return -1;

    char elf_path_ipc[LINUX_PATH_MAX] = {0};
    const char *ep = proc_get_elf_path();
    if (ep)
        str_copy_trunc(elf_path_ipc, ep, sizeof(elf_path_ipc));
    char elfuse_path_ipc[LINUX_PATH_MAX] = {0};
    const char *hp = proc_get_elfuse_path();
    if (hp)
        str_copy_trunc(elfuse_path_ipc, hp, sizeof(elfuse_path_ipc));
    if (fork_ipc_write_all(ipc_sock, elf_path_ipc, sizeof(elf_path_ipc)) < 0 ||
        fork_ipc_write_all(ipc_sock, elfuse_path_ipc, sizeof(elfuse_path_ipc)) <
            0)
        return -1;

    size_t cmdline_len = 0;
    const char *cmdline = proc_get_cmdline(&cmdline_len);
    uint32_t cmdline_len_u32 = (uint32_t) cmdline_len;
    if (fork_ipc_write_all(ipc_sock, &cmdline_len_u32,
                           sizeof(cmdline_len_u32)) < 0)
        return -1;
    if (cmdline_len_u32 > 0 && cmdline &&
        fork_ipc_write_all(ipc_sock, cmdline, cmdline_len_u32) < 0)
        return -1;

    if (fork_ipc_write_all(ipc_sock, &num_guest_regions,
                           sizeof(num_guest_regions)) < 0)
        return -1;
    if (num_guest_regions > 0 &&
        fork_ipc_write_all(ipc_sock, regions_snapshot,
                           num_guest_regions * sizeof(guest_region_t)) < 0)
        return -1;

    if (fork_ipc_send_backing_fds(ipc_sock, regions_snapshot,
                                  num_guest_regions) < 0)
        return -1;

    const signal_state_t *sig = signal_get_state();
    if (fork_ipc_write_all(ipc_sock, sig, sizeof(signal_state_t)) < 0)
        return -1;

    const unsigned char *shim_ptr = proc_get_shim_blob();
    uint32_t shim_size_u32 = proc_get_shim_size();
    if (fork_ipc_write_all(ipc_sock, &shim_size_u32, sizeof(shim_size_u32)) < 0)
        return -1;
    if (shim_size_u32 > 0 && shim_ptr &&
        fork_ipc_write_all(ipc_sock, shim_ptr, shim_size_u32) < 0)
        return -1;

    uint32_t sentinel = IPC_MAGIC_SENTINEL;
    return fork_ipc_write_all(ipc_sock, &sentinel, sizeof(sentinel));
}

static int fork_ipc_drain_bytes(int ipc_fd, uint32_t len)
{
    char drain[256];
    uint32_t remaining = len;
    while (remaining > 0) {
        uint32_t chunk = remaining < sizeof(drain) ? remaining : sizeof(drain);
        if (fork_ipc_read_all(ipc_fd, drain, chunk) < 0)
            return -1;
        remaining -= chunk;
    }
    return 0;
}

static int fork_ipc_recv_backing_fds(int ipc_fd, guest_t *g)
{
    uint32_t nbacking;
    if (fork_ipc_read_all(ipc_fd, &nbacking, sizeof(nbacking)) < 0) {
        log_error("fork-child: failed to read backing fd count");
        return -1;
    }
    if (nbacking == 0 || nbacking > GUEST_MAX_REGIONS)
        return 0;

    char dummy;
    struct iovec iov = {.iov_base = &dummy, .iov_len = 1};
    size_t cmsg_sz = CMSG_SPACE(nbacking * sizeof(int));
    uint8_t *cmsg_buf = calloc(1, cmsg_sz);
    if (!cmsg_buf)
        return -1;

    struct msghdr msg = {
        .msg_iov = &iov,
        .msg_iovlen = 1,
        .msg_control = cmsg_buf,
        .msg_controllen = cmsg_sz,
    };
    ssize_t nr = recvmsg(ipc_fd, &msg, 0);
    if (nr > 0) {
        struct cmsghdr *cmsg = CMSG_FIRSTHDR(&msg);
        if (cmsg && cmsg->cmsg_level == SOL_SOCKET &&
            cmsg->cmsg_type == SCM_RIGHTS) {
            int *region_fds = (int *) CMSG_DATA(cmsg);
            uint32_t fi = 0;
            for (int i = 0; i < g->nregions && fi < nbacking; i++) {
                if (!(g->regions[i].flags & LINUX_MAP_ANONYMOUS) &&
                    g->regions[i].offset != (uint64_t) -1) {
                    g->regions[i].backing_fd = region_fds[fi++];
                }
            }
        }
    }
    free(cmsg_buf);
    return 0;
}

int fork_ipc_recv_process_state(int ipc_fd, guest_t *g, signal_state_t *sig)
{
    char cwd[LINUX_PATH_MAX];
    uint32_t umask_val;
    if (fork_ipc_read_all(ipc_fd, cwd, sizeof(cwd)) < 0 ||
        fork_ipc_read_all(ipc_fd, &umask_val, sizeof(umask_val)) < 0) {
        log_error("fork-child: failed to read process info");
        return -1;
    }
    if (cwd[0] != '\0')
        chdir(cwd);
    umask((mode_t) umask_val);

    char sysroot_ipc[LINUX_PATH_MAX];
    if (fork_ipc_read_all(ipc_fd, sysroot_ipc, sizeof(sysroot_ipc)) < 0) {
        log_error("fork-child: failed to read sysroot");
        return -1;
    }
    if (sysroot_ipc[0] != '\0')
        proc_set_sysroot(sysroot_ipc);

    char elf_path_ipc[LINUX_PATH_MAX];
    if (fork_ipc_read_all(ipc_fd, elf_path_ipc, sizeof(elf_path_ipc)) < 0) {
        log_error("fork-child: failed to read elf path");
        return -1;
    }
    if (elf_path_ipc[0] != '\0')
        proc_set_elf_path(elf_path_ipc);

    char elfuse_path_ipc[LINUX_PATH_MAX];
    if (fork_ipc_read_all(ipc_fd, elfuse_path_ipc, sizeof(elfuse_path_ipc)) <
        0) {
        log_error("fork-child: failed to read elfuse path");
        return -1;
    }
    if (elfuse_path_ipc[0] != '\0')
        proc_set_elfuse_path(elfuse_path_ipc);

    uint32_t cmdline_len_u32;
    if (fork_ipc_read_all(ipc_fd, &cmdline_len_u32, sizeof(cmdline_len_u32)) <
        0) {
        log_error("fork-child: failed to read cmdline len");
        return -1;
    }
    if (cmdline_len_u32 > 0) {
        char *cmdline_buf = (cmdline_len_u32 <= LINUX_PATH_MAX * 4)
                                ? malloc(cmdline_len_u32)
                                : NULL;
        if (cmdline_buf) {
            if (fork_ipc_read_all(ipc_fd, cmdline_buf, cmdline_len_u32) < 0) {
                free(cmdline_buf);
                log_error("fork-child: failed to read cmdline");
                return -1;
            }
            proc_set_cmdline_raw(cmdline_buf, cmdline_len_u32);
            free(cmdline_buf);
        } else {
            if (cmdline_len_u32 > LINUX_PATH_MAX * 4)
                log_warn("fork-child: cmdline too large (%u), skipping",
                         cmdline_len_u32);
            else
                log_error("fork-child: cmdline malloc failed");
            if (fork_ipc_drain_bytes(ipc_fd, cmdline_len_u32) < 0)
                return -1;
        }
    }

    uint32_t num_guest_regions;
    if (fork_ipc_read_all(ipc_fd, &num_guest_regions,
                          sizeof(num_guest_regions)) < 0) {
        log_error("fork-child: failed to read region count");
        return -1;
    }
    if (num_guest_regions > GUEST_MAX_REGIONS)
        num_guest_regions = GUEST_MAX_REGIONS;
    if (num_guest_regions > 0 &&
        fork_ipc_read_all(ipc_fd, g->regions,
                          num_guest_regions * sizeof(guest_region_t)) < 0) {
        log_error("fork-child: failed to read regions");
        return -1;
    }
    g->nregions = (int) num_guest_regions;
    for (int i = 0; i < g->nregions; i++) {
        g->regions[i].backing_fd = -1;
        /* Demote inherited overlays: the child does not yet re-establish
         * host MAP_FIXED|MAP_SHARED mappings from the parent's overlay
         * fds, so msync, MADV_DONTNEED and friends must use the
         * snapshot-style emulation. The CoW path's pre-fork sync of
         * overlay bytes into shm_fd already gave the child snapshot the
         * correct content at fork time. Live cross-fork MAP_SHARED
         * coherence is the next P1 TODO item.
         */
        g->regions[i].overlay_active = false;
        g->regions[i].overlay_start = 0;
        g->regions[i].overlay_end = 0;
    }

    if (fork_ipc_recv_backing_fds(ipc_fd, g) < 0)
        return -1;

    if (fork_ipc_read_all(ipc_fd, sig, sizeof(*sig)) < 0) {
        log_error("fork-child: failed to read signal state");
        return -1;
    }

    uint32_t shim_size;
    if (fork_ipc_read_all(ipc_fd, &shim_size, sizeof(shim_size)) < 0) {
        log_error("fork-child: failed to read shim size");
        return -1;
    }
    if (shim_size > 0) {
        unsigned char *shim = malloc(shim_size);
        if (!shim) {
            log_error("fork-child: shim alloc failed");
            return -1;
        }
        if (fork_ipc_read_all(ipc_fd, shim, shim_size) < 0) {
            log_error("fork-child: failed to read shim blob");
            free(shim);
            return -1;
        }
        proc_set_shim_owned(shim, shim_size);
    }

    uint32_t sentinel;
    if (fork_ipc_read_all(ipc_fd, &sentinel, sizeof(sentinel)) < 0 ||
        sentinel != IPC_MAGIC_SENTINEL) {
        log_error("fork-child: bad sentinel");
        return -1;
    }

    return 0;
}
