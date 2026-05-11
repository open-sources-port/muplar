/* Fork IPC state serialization
 *
 * Copyright 2026 elfuse contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "hvutil.h"

#include "core/guest.h"
#include "syscall/abi.h"
#include "syscall/signal.h"

/* Magic values for IPC frame delimiters */
#define IPC_MAGIC_HEADER 0x454C464BU   /* "ELFK" */
#define IPC_MAGIC_SENTINEL 0x454C4F4BU /* "ELOK" */
#define IPC_VERSION 9                  /* v9: preserve elf_load_min */

typedef struct {
    uint32_t magic;
    uint32_t version;
    uint32_t ipa_bits;
    uint32_t has_shm;
    int64_t child_pid, parent_pid;
    uint64_t guest_size;
    uint64_t elf_load_min;
    uint64_t brk_base, brk_current;
    uint64_t stack_base;
    uint64_t stack_top;
    uint64_t mmap_next, mmap_end, pt_pool_next, ttbr0;
    uint64_t mmap_rx_next;
    uint64_t mmap_rx_end;
    uint32_t uid, euid, suid, gid, egid, sgid;
    int32_t nice;
    uint32_t _pad;
    uint64_t absock_namespace_id;
    int64_t sid, pgid;
} ipc_header_t;

typedef struct {
    uint64_t elr_el1, sp_el0;
    uint64_t spsr_el1, vbar_el1;
    uint64_t ttbr0_el1;
    uint64_t sctlr_el1, tcr_el1, mair_el1, cpacr_el1, tpidr_el0, sp_el1;
    uint64_t x[31];
    vcpu_simd_state_t simd_state;
} ipc_registers_t;

typedef struct {
    uint64_t offset, size;
} ipc_region_header_t;

typedef struct {
    int32_t guest_fd, type, linux_flags, seals;
    char proc_path[FD_VIRTUAL_PATH_MAX];
} ipc_fd_entry_t;

int fork_ipc_write_all(int fd, const void *buf, size_t len);
int fork_ipc_read_all(int fd, void *buf, size_t len);
int fork_ipc_send_fds(int sock, const int *fds, int count);
int fork_ipc_recv_fds(int sock, int *fds, int max_count, int *out_count);

int fork_ipc_send_memory_regions(int ipc_sock, const guest_t *g, bool use_shm);
int fork_ipc_recv_memory_regions(int ipc_fd, guest_t *g);

int fork_ipc_send_fd_table(int ipc_sock);
int fork_ipc_recv_fd_table(int ipc_fd, guest_t *g);

int fork_ipc_send_process_state(int ipc_sock,
                                const guest_region_t *regions_snapshot,
                                uint32_t num_guest_regions);
int fork_ipc_recv_process_state(int ipc_fd, guest_t *g, signal_state_t *sig);
