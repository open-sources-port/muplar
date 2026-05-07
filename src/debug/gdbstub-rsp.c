/* GDB RSP transport and hex helpers
 *
 * Copyright 2026 elfuse contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/uio.h>
#include <unistd.h>
#include <errno.h>

#include "debug/gdbstub-rsp.h"

static const char hex_chars[] = "0123456789abcdef";

static int hex_val(char c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return -1;
}

int gdb_hex_encode(char *dst, const uint8_t *src, size_t len)
{
    for (size_t i = 0; i < len; i++) {
        dst[i * 2] = hex_chars[(src[i] >> 4) & 0xF];
        dst[i * 2 + 1] = hex_chars[src[i] & 0xF];
    }
    dst[len * 2] = '\0';
    return (int) (len * 2);
}

int gdb_hex_decode(uint8_t *dst, const char *src, size_t len)
{
    for (size_t i = 0; i < len; i++) {
        int hi = hex_val(src[i * 2]), lo = hex_val(src[i * 2 + 1]);
        if (hi < 0 || lo < 0)
            return -1;
        dst[i] = (uint8_t) ((hi << 4) | lo);
    }
    return (int) len;
}

uint64_t gdb_parse_hex(const char **pp)
{
    const char *p = *pp;
    uint64_t val = 0;
    while (1) {
        int d = hex_val(*p);
        if (d < 0)
            break;
        val = (val << 4) | (uint64_t) d;
        p++;
    }
    *pp = p;
    return val;
}

static uint8_t rsp_checksum(const char *data, size_t len)
{
    uint8_t sum = 0;
    for (size_t i = 0; i < len; i++)
        sum += (uint8_t) data[i];
    return sum;
}

static int rsp_send_byte(int fd, char value)
{
    while (1) {
        ssize_t n = write(fd, &value, 1);
        if (n < 0) {
            if (errno == EINTR)
                continue;
            return -1;
        }
        if (n == 1)
            return 0;
        errno = EIO;
        return -1;
    }
}

int gdb_rsp_send(int fd, const char *data, size_t len)
{
    uint8_t cksum = rsp_checksum(data, len);
    char hdr = '$';
    char trailer[3];
    trailer[0] = '#';
    trailer[1] = hex_chars[(cksum >> 4) & 0xF];
    trailer[2] = hex_chars[cksum & 0xF];

    struct iovec iov[3] = {
        {.iov_base = &hdr, .iov_len = 1},
        {.iov_base = (void *) data, .iov_len = len},
        {.iov_base = trailer, .iov_len = 3},
    };

    int iovcnt = 3;
    struct iovec *cur = iov;
    size_t total = 1 + len + 3;

    while (total > 0) {
        ssize_t written = writev(fd, cur, iovcnt);
        if (written < 0) {
            if (errno == EINTR)
                continue;
            return -1;
        }
        if (written == 0)
            return -1;
        total -= (size_t) written;
        while (iovcnt > 0 && (size_t) written >= cur->iov_len) {
            written -= (ssize_t) cur->iov_len;
            cur++;
            iovcnt--;
        }
        if (iovcnt > 0 && written > 0) {
            cur->iov_base = (char *) cur->iov_base + written;
            cur->iov_len -= (size_t) written;
        }
    }
    return 0;
}

void gdb_rsp_reset(gdb_rsp_ctx_t *ctx)
{
    ctx->read_pos = 0;
    ctx->read_len = 0;
    ctx->no_ack_mode = false;
}

void gdb_rsp_set_noack(gdb_rsp_ctx_t *ctx, bool enabled)
{
    ctx->no_ack_mode = enabled;
}

int gdb_rsp_recv(gdb_rsp_ctx_t *ctx, int fd, char *buf, size_t bufsz)
{
    if (bufsz == 0) {
        errno = EINVAL;
        return -1;
    }

    int state = 0;
    size_t pos = 0;
    char ck_hi = 0, ck_lo = 0;
    bool overflow = false;

    while (1) {
        if (ctx->read_pos >= ctx->read_len) {
            ssize_t n = read(fd, ctx->read_buf, GDB_RSP_READ_BUF_SIZE);
            if (n <= 0) {
                if (n < 0 && errno == EINTR)
                    continue;
                return (n == 0) ? 0 : -1;
            }
            ctx->read_pos = 0;
            ctx->read_len = (size_t) n;
        }

        while (ctx->read_pos < ctx->read_len) {
            uint8_t c = ctx->read_buf[ctx->read_pos++];

            if (state == 0 && (c == '+' || c == '-'))
                continue;

            if (c == 0x03) {
                buf[0] = 0x03;
                return 1;
            }

            switch (state) {
            case 0:
                if (c == '$') {
                    state = 1;
                    pos = 0;
                    overflow = false;
                }
                break;
            case 1:
                if (c == '#') {
                    state = 2;
                    break;
                }
                if (!overflow) {
                    if (pos < bufsz - 1) {
                        buf[pos++] = (char) c;
                    } else {
                        overflow = true;
                    }
                }
                break;
            case 2:
                ck_hi = (char) c;
                state = 3;
                break;
            case 3: {
                ck_lo = (char) c;

                if (overflow) {
                    gdb_rsp_send(fd, "E00", 3);
                    state = 0;
                    pos = 0;
                    overflow = false;
                    break;
                }

                buf[pos] = '\0';

                uint8_t expected =
                    (uint8_t) ((hex_val(ck_hi) << 4) | hex_val(ck_lo));
                uint8_t actual = rsp_checksum(buf, pos);
                if (expected == actual) {
                    if (!ctx->no_ack_mode)
                        (void) rsp_send_byte(fd, '+');
                    return (int) pos;
                } else {
                    if (!ctx->no_ack_mode)
                        (void) rsp_send_byte(fd, '-');
                    state = 0;
                    pos = 0;
                }
                break;
            }
            default:
                state = 0;
                break;
            }
        }
    }
}
