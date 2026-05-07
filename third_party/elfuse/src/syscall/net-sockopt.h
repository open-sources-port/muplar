/* Socket option cache helpers
 *
 * Copyright 2026 elfuse contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

int net_socket_fd_is_valid(int guest_fd);
int net_socket_cached_int_get(int guest_fd, int level, int optname, int *value);
void net_socket_cached_int_set(int guest_fd, int level, int optname, int value);
void net_socket_cache_set_index(int guest_fd, int idx, int value);
void net_socket_cache_init_defaults(int guest_fd, int domain, int real_type);
void net_socket_cache_init_accept(int guest_fd);
