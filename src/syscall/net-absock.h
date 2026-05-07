/* Abstract AF_UNIX emulation helpers
 *
 * Copyright 2026 elfuse contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <stdint.h>
#include <sys/socket.h>

int absock_is_abstract_unix(const uint8_t *linux_sa, uint32_t addrlen);
int absock_rewrite_connect(const uint8_t *linux_sa,
                           uint32_t addrlen,
                           struct sockaddr_storage *mac_sa);
int absock_bind_prepare(const uint8_t *linux_sa,
                        uint32_t addrlen,
                        struct sockaddr_storage *mac_sa,
                        int guest_fd,
                        int *out_len);
void absock_bind_commit(int idx);
void absock_bind_rollback(int idx);
int absock_reverse_lookup(const char *fs_path,
                          uint8_t *out_name,
                          uint32_t *out_len);
void absock_init_cleanup(void);
