/* Socket ABI translation helpers
 *
 * Copyright 2026 elfuse contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <netinet/in.h>
#include <stdint.h>
#include <sys/socket.h>

int socket_small_int_normalize(int level, int optname, int value);
int socket_opt_uses_small_int(int level, int optname);
int translate_small_int_sockopt(int level,
                                int optname,
                                int *mac_level,
                                int *mac_optname);
int translate_af_to_mac(int linux_af);
int linux_to_mac_sockaddr(const void *linux_sa,
                          uint32_t linux_len,
                          struct sockaddr_storage *mac_sa);
int mac_to_linux_sockaddr(const struct sockaddr *mac_sa,
                          socklen_t mac_len,
                          uint8_t *linux_sa,
                          uint32_t linux_buf_len);
int extract_sock_type(int linux_type);
int extract_sock_nonblock(int linux_type);
int extract_sock_cloexec(int linux_type);
int translate_sockopt(int linux_optname);
int translate_ip_sockopt_to_mac(int linux_optname);
int translate_ip_cmsg_to_mac(int linux_type);
int translate_ip_cmsg_to_linux(int mac_type);
int translate_msg_flags(int linux_flags);
int mac_to_linux_msg_flags(int mac_flags);
int sockaddr_has_zero_port(const struct sockaddr_storage *sa);
void sockaddr_set_port(struct sockaddr_storage *sa, in_port_t port);
