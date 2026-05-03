/* SPDX-License-Identifier: GPL-2.0 */
#pragma once

/*
 * Linux classic BPF compatibility layer for non-Linux systems (macOS).
 *
 * This header allows code that builds Linux socket filters
 * to compile, but NOT to execute.
 */

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Instruction classes */
#define BPF_LD      0x00
#define BPF_LDX     0x01
#define BPF_ST      0x02
#define BPF_STX     0x03
#define BPF_ALU     0x04
#define BPF_JMP     0x05
#define BPF_RET     0x06
#define BPF_MISC    0x07

/* ld/ldx fields */
#define BPF_W       0x00
#define BPF_H       0x08
#define BPF_B       0x10

/* ld/ldx modes */
#define BPF_IMM     0x00
#define BPF_ABS     0x20
#define BPF_IND     0x40
#define BPF_MEM     0x60
#define BPF_LEN     0x80
#define BPF_MSH     0xa0

/* alu/jmp operations */
#define BPF_ADD     0x00
#define BPF_SUB     0x10
#define BPF_MUL     0x20
#define BPF_DIV     0x30
#define BPF_OR      0x40
#define BPF_AND     0x50
#define BPF_LSH     0x60
#define BPF_RSH     0x70
#define BPF_NEG     0x80
#define BPF_MOD     0x90
#define BPF_XOR     0xa0

/* jmp conditions */
#define BPF_JA      0x00
#define BPF_JEQ     0x10
#define BPF_JGT     0x20
#define BPF_JGE     0x30
#define BPF_JSET    0x40

/* src operands */
#define BPF_K       0x00
#define BPF_X       0x08

/* ret - return value */
#define BPF_A       0x10

/* misc */
#define BPF_TAX     0x00
#define BPF_TXA     0x80

/* Helper macros */
#define BPF_CLASS(code) ((code) & 0x07)
#define BPF_SIZE(code)  ((code) & 0x18)
#define BPF_MODE(code)  ((code) & 0xe0)
#define BPF_OP(code)    ((code) & 0xf0)
#define BPF_SRC(code)   ((code) & 0x08)

/* Filter instruction */
struct sock_filter {
        uint16_t code;
        uint8_t  jt;
        uint8_t  jf;
        uint32_t k;
};

/* Filter program */
struct sock_fprog {
        unsigned short len;
        struct sock_filter *filter;
};

/* Helpers */
// #define BPF_STMT(code, k) \
//         ((struct sock_filter){ (uint16_t)(code), 0, 0, (uint32_t)(k) })

// #define BPF_JUMP(code, k, jt, jf) \
//         ((struct sock_filter){ (uint16_t)(code), (jt), (jf), (uint32_t)(k) })

#ifndef SCM_CREDENTIALS
#define SCM_CREDENTIALS 0
#endif

#ifndef SO_ATTACH_FILTER
#define SO_ATTACH_FILTER 0
#endif

#ifndef SO_DETACH_FILTER
#define SO_DETACH_FILTER 0
#endif

#ifdef __cplusplus
}
#endif
