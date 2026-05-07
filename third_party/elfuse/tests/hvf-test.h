/* Shared helpers for native HVF tests
 *
 * Copyright 2026 elfuse contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <assert.h>
#include <stdint.h>
#include <string.h>

enum {
    HVF_EXIT_HVC5 = 0,
    HVF_EXIT_HVC0 = 1,
    HVF_EXIT_HVC2 = 2,
    HVF_EXIT_ERROR = 3,
    HVF_EXIT_HVC9 = 9,
};

/* AArch64 instruction encoders.
 *
 * Three families share an encoding shape; the lists below drive a token-
 * pasted `a64_<name>` definition for each entry, eliminating the per-
 * instruction boilerplate. Each encoder asserts its operand preconditions
 * so a bad caller surfaces immediately instead of silently emitting a
 * different (but still valid) instruction.
 */

/* MOVZ / MOVK family: opcode | (imm16 << 5) | rd */
#define A64_INSN_IMM16(_) \
    _(movz, 0xD2800000U)  \
    _(movk_lsl16, 0xF2A00000U)

/* Bare encodings with no operands */
#define A64_INSN_CONST(_)   \
    _(svc0, 0xD4000001U)    \
    _(dsb_ish, 0xD5033F9FU) \
    _(isb, 0xD5033FDFU)

/* Load/store unsigned-offset: opcode | (off/scale << 10) | (rn << 5) | rt */
#define A64_INSN_LS_IMM(_)       \
    _(str_imm64, 0xF9000000U, 8) \
    _(ldr_imm64, 0xF9400000U, 8) \
    _(str_w_imm, 0xB9000000U, 4)

#define _(name, base)                                          \
    static inline uint32_t a64_##name(int rd, uint16_t imm)    \
    {                                                          \
        assert(rd >= 0 && rd <= 31);                           \
        return (base) | ((uint32_t) imm << 5) | (uint32_t) rd; \
    }
A64_INSN_IMM16(_)
#undef _

#define _(name, base)                       \
    static inline uint32_t a64_##name(void) \
    {                                       \
        return (base);                      \
    }
A64_INSN_CONST(_)
#undef _

#define _(name, base, scale)                                                  \
    static inline uint32_t a64_##name(int rt, int rn, uint32_t byte_off)      \
    {                                                                         \
        assert(rt >= 0 && rt <= 31);                                          \
        assert(rn >= 0 && rn <= 31);                                          \
        assert(byte_off % (scale) == 0);                                      \
        uint32_t imm12 = byte_off / (scale);                                  \
        assert(imm12 <= 0xFFF);                                               \
        return (base) | (imm12 << 10) | ((uint32_t) rn << 5) | (uint32_t) rt; \
    }
A64_INSN_LS_IMM(_)
#undef _

/* Singletons (no shared pattern). */

static inline uint32_t a64_mov_reg(int rd, int rm)
{
    assert(rd >= 0 && rd <= 31);
    assert(rm >= 0 && rm <= 31);
    return 0xAA0003E0U | ((uint32_t) rm << 16) | (uint32_t) rd;
}

static inline uint32_t a64_br(int rn)
{
    assert(rn >= 0 && rn <= 31);
    return 0xD61F0000U | ((uint32_t) rn << 5);
}

static inline void hvf_emit_code(void *base,
                                 uint64_t offset,
                                 const uint32_t *insns,
                                 size_t count)
{
    memcpy((uint8_t *) base + offset, insns, count * sizeof(*insns));
}
