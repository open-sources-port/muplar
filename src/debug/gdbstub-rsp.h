/* GDB RSP transport and hex helpers
 *
 * Copyright 2026 elfuse contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define GDB_RSP_READ_BUF_SIZE 4096

typedef struct {
    uint8_t read_buf[GDB_RSP_READ_BUF_SIZE];
    size_t read_pos;
    size_t read_len;
    bool no_ack_mode;
} gdb_rsp_ctx_t;

int gdb_hex_encode(char *dst, const uint8_t *src, size_t len);
int gdb_hex_decode(uint8_t *dst, const char *src, size_t len);
uint64_t gdb_parse_hex(const char **pp);

int gdb_rsp_send(int fd, const char *data, size_t len);
void gdb_rsp_reset(gdb_rsp_ctx_t *ctx);
void gdb_rsp_set_noack(gdb_rsp_ctx_t *ctx, bool enabled);
int gdb_rsp_recv(gdb_rsp_ctx_t *ctx, int fd, char *buf, size_t bufsz);
