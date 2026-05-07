/* vDSO ELF image
 *
 * Copyright 2026 elfuse contributors
 * Copyright 2025 Moritz Angermann, zw3rk pte. ltd.
 * SPDX-License-Identifier: Apache-2.0
 *
 * Builds a minimal vDSO ELF image in guest memory exposing
 * __kernel_{rt_sigreturn,clock_getres,clock_gettime,gettimeofday}. Each entry
 * point is an SVC trampoline that traps back to the host for the actual work.
 *
 * An earlier revision had a CNTVCT-based fast path for clock_gettime backed by
 * a host-updated vvar page. That path was incorrect under HVF: the host writes
 * CNTVCT_EL0 from the macOS frame of reference while the guest reads it through
 * HVF's CNTVOFF_EL2 virtualization, so the seqlock interpolation produced bogus
 * times (year 26382). The fast path is gone; SVC is correct and the trap cost
 * is negligible compared to the work clock_gettime callers tend to do anyway.
 */

#include <stdint.h>
#include <string.h>

#include "core/vdso.h"
#include "core/elf.h"
#include "debug/log.h"

/* ELF section header (not in core/elf.h). */

typedef struct {
    uint32_t sh_name, sh_type;
    uint64_t sh_flags, sh_addr, sh_offset, sh_size;
    uint32_t sh_link, sh_info;
    uint64_t sh_addralign, sh_entsize;
} elf64_shdr_t;

typedef struct {
    int64_t d_tag;
    uint64_t d_val;
} elf64_dyn_t;

typedef struct {
    uint32_t st_name;
    uint8_t st_info, st_other;
    uint16_t st_shndx;
    uint64_t st_value, st_size;
} elf64_sym_t;

/* ELF constants */
#define SHT_STRTAB 3
#define SHT_HASH 5
#define SHT_DYNAMIC 6
#define SHT_DYNSYM 11
#define SHF_ALLOC (1ULL << 1)
#define SHF_EXECINSTR (1ULL << 2)
#define DT_NULL 0
#define DT_HASH 4
#define DT_STRTAB 5
#define DT_SYMTAB 6
#define DT_STRSZ 10
#define DT_SYMENT 11
#define STB_GLOBAL 1
#define STT_FUNC 2
#define ELF_ST_INFO(bind, type) (((bind) << 4) | ((type) & 0xf))

/* Layout.
 *
 * Symbol layout (all entries are 12-byte SVC trampolines):
 *   [0] __kernel_rt_sigreturn
 *   [1] __kernel_clock_getres
 *   [2] __kernel_clock_gettime
 *   [3] __kernel_gettimeofday
 */

/* Offsets within the 4KiB page */
#define VDSO_OFF_EHDR 0x000
#define VDSO_OFF_PHDR 0x040
#define VDSO_OFF_PHDR1 0x078

/* .text trampolines (each 12 bytes: mov x8, #N; svc #0; ret). */
#define TEXT_OFF_SIGRET 0x0B0
#define TEXT_OFF_GETRES 0x0BC
#define TEXT_OFF_GETTIME 0x0C8
#define TEXT_OFF_GETTOD 0x0D4
#define TEXT_END 0x0E0

/* dynstr, dynsym, hash, dynamic, shdr follow */
#define VDSO_OFF_DYNSTR 0x0E0
#define DYNSTR_SIZE 90

/* Padded to 4-byte align: 0x0E0 + 90 = 0x13A, pad to 0x13C */
#define VDSO_OFF_DYNSYM 0x13C

/* 5 * 24 = 120, 0x13C + 120 = 0x1B4 */
#define VDSO_OFF_HASH 0x1B4

/* 2+1+5 = 8 words * 4 = 32, 0x1B4 + 32 = 0x1D4, pad to 0x1D8 */
#define VDSO_OFF_DYNAMIC 0x1D8

/* 6 * 16 = 96, 0x1D8 + 96 = 0x238 */
#define VDSO_OFF_SHDR 0x238

/* 6 * 64 = 384, 0x238 + 384 = 0x3B8 (fits in 4KiB) */
#define VDSO_NUM_SYMS 4
#define HASH_NCHAIN (VDSO_NUM_SYMS + 1)
#define HASH_NBUCKET 1
#define HASH_SIZE ((2 + HASH_NBUCKET + HASH_NCHAIN) * sizeof(uint32_t))

/* .dynstr data */
static const char dynstr_data[] =
    "\0__kernel_rt_sigreturn"
    "\0__kernel_clock_getres"
    "\0__kernel_clock_gettime"
    "\0__kernel_gettimeofday";

/* Symbol name offsets */
static const uint32_t sym_name_offsets[VDSO_NUM_SYMS] = {1, 23, 45, 68};

/* Symbol text offsets and sizes */
static const uint32_t sym_text_off[VDSO_NUM_SYMS] = {
    TEXT_OFF_SIGRET, TEXT_OFF_GETRES, TEXT_OFF_GETTIME, TEXT_OFF_GETTOD};
static const uint32_t sym_text_size[VDSO_NUM_SYMS] = {
    12, 12, TEXT_OFF_GETTOD - TEXT_OFF_GETTIME, 12};

/* Emit a 12-byte SVC trampoline: mov x8, #syscall_nr; svc #0; ret. */
static void emit_svc_trampoline(uint32_t *code, unsigned syscall_nr)
{
    /* MOVZ Xd, #imm16, LSL #0: encoding 0xD2800000 | (imm16<<5) | rd. */
    code[0] = 0xD2800000U | (((uint32_t) syscall_nr & 0xFFFF) << 5) | 8;
    code[1] = 0xD4000001U; /* svc #0 */
    code[2] = 0xD65F03C0U; /* ret    */
}

uint64_t vdso_build(guest_t *g)
{
    uint8_t *page = (uint8_t *) guest_ptr(g, VDSO_BASE);
    if (!page) {
        log_error("vdso: VDSO_BASE 0x%llx out of guest memory",
                  (unsigned long long) VDSO_BASE);
        return 0;
    }

    memset(page, 0, VDSO_SIZE);

    /* ELF header. */
    elf64_ehdr_t *ehdr = (elf64_ehdr_t *) (page + VDSO_OFF_EHDR);
    ehdr->e_ident[0] = ELFMAG0;
    ehdr->e_ident[1] = ELFMAG1;
    ehdr->e_ident[2] = ELFMAG2;
    ehdr->e_ident[3] = ELFMAG3;
    ehdr->e_ident[EI_CLASS] = ELFCLASS64;
    ehdr->e_ident[EI_DATA] = ELFDATA2LSB;
    ehdr->e_ident[6] = 1;
    ehdr->e_type = ET_DYN;
    ehdr->e_machine = EM_AARCH64;
    ehdr->e_version = 1;
    ehdr->e_entry = TEXT_OFF_SIGRET;
    ehdr->e_phoff = VDSO_OFF_PHDR;
    ehdr->e_shoff = VDSO_OFF_SHDR;
    ehdr->e_flags = 0;
    ehdr->e_ehsize = sizeof(elf64_ehdr_t);
    ehdr->e_phentsize = sizeof(elf64_phdr_t);
    ehdr->e_phnum = 2;
    ehdr->e_shentsize = sizeof(elf64_shdr_t);
    ehdr->e_shnum = 6;
    ehdr->e_shstrndx = 2;

    /* Program header 0: PT_LOAD. */
    elf64_phdr_t *phdr0 = (elf64_phdr_t *) (page + VDSO_OFF_PHDR);
    phdr0->p_type = PT_LOAD;
    phdr0->p_flags = PF_R | PF_X;
    phdr0->p_offset = 0;
    phdr0->p_vaddr = 0;
    phdr0->p_paddr = 0;
    phdr0->p_filesz = VDSO_SIZE;
    phdr0->p_memsz = VDSO_SIZE;
    phdr0->p_align = 0x1000;

    /* Program header 1: PT_DYNAMIC. */
    elf64_phdr_t *phdr1 = (elf64_phdr_t *) (page + VDSO_OFF_PHDR1);
    phdr1->p_type = PT_DYNAMIC;
    phdr1->p_flags = PF_R;
    phdr1->p_offset = VDSO_OFF_DYNAMIC;
    phdr1->p_vaddr = VDSO_OFF_DYNAMIC;
    phdr1->p_paddr = VDSO_OFF_DYNAMIC;
    phdr1->p_filesz = 6 * sizeof(elf64_dyn_t);
    phdr1->p_memsz = 6 * sizeof(elf64_dyn_t);
    phdr1->p_align = 8;

    /* Text trampolines.  Each entry is the same 12-byte mov/svc/ret pattern
     * with the syscall number patched in.
     */
    emit_svc_trampoline((uint32_t *) (page + TEXT_OFF_SIGRET), 139);
    emit_svc_trampoline((uint32_t *) (page + TEXT_OFF_GETRES), 114);
    emit_svc_trampoline((uint32_t *) (page + TEXT_OFF_GETTIME), 113);
    emit_svc_trampoline((uint32_t *) (page + TEXT_OFF_GETTOD), 169);

    /* Dynamic string table. */
    memcpy(page + VDSO_OFF_DYNSTR, dynstr_data, DYNSTR_SIZE);

    /* Dynamic symbol table. */
    elf64_sym_t *sym = (elf64_sym_t *) (page + VDSO_OFF_DYNSYM);
    memset(&sym[0], 0, sizeof(elf64_sym_t));
    for (int i = 0; i < VDSO_NUM_SYMS; i++) {
        sym[i + 1].st_name = sym_name_offsets[i];
        sym[i + 1].st_info = ELF_ST_INFO(STB_GLOBAL, STT_FUNC);
        sym[i + 1].st_other = 0;
        sym[i + 1].st_shndx = 1;
        sym[i + 1].st_value = sym_text_off[i];
        sym[i + 1].st_size = sym_text_size[i];
    }

    /* ELF hash table. */
    uint32_t *hash = (uint32_t *) (page + VDSO_OFF_HASH);
    hash[0] = HASH_NBUCKET, hash[1] = HASH_NCHAIN;
    hash[2] = 0;
    uint32_t *chain = &hash[2 + HASH_NBUCKET];
    memset(chain, 0, HASH_NCHAIN * sizeof(uint32_t));
    uint32_t first_sym = 0;
    for (int i = VDSO_NUM_SYMS; i >= 1; i--) {
        chain[i] = first_sym;
        first_sym = (uint32_t) i;
    }
    hash[2] = first_sym;

    /* Dynamic table. */
    elf64_dyn_t *dyn = (elf64_dyn_t *) (page + VDSO_OFF_DYNAMIC);
    dyn[0] = (elf64_dyn_t) {DT_HASH, VDSO_OFF_HASH};
    dyn[1] = (elf64_dyn_t) {DT_SYMTAB, VDSO_OFF_DYNSYM};
    dyn[2] = (elf64_dyn_t) {DT_STRTAB, VDSO_OFF_DYNSTR};
    dyn[3] = (elf64_dyn_t) {DT_STRSZ, DYNSTR_SIZE};
    dyn[4] = (elf64_dyn_t) {DT_SYMENT, sizeof(elf64_sym_t)};
    dyn[5] = (elf64_dyn_t) {DT_NULL, 0};

    /* Section headers. */
    elf64_shdr_t *shdr = (elf64_shdr_t *) (page + VDSO_OFF_SHDR);
    memset(&shdr[0], 0, sizeof(elf64_shdr_t));

    shdr[1].sh_name = 0;
    shdr[1].sh_type = 1; /* SHT_PROGBITS */
    shdr[1].sh_flags = SHF_ALLOC | SHF_EXECINSTR;
    shdr[1].sh_addr = TEXT_OFF_SIGRET;
    shdr[1].sh_offset = TEXT_OFF_SIGRET;
    shdr[1].sh_size = TEXT_END - TEXT_OFF_SIGRET;
    shdr[1].sh_addralign = 4;

    shdr[2].sh_name = 0;
    shdr[2].sh_type = SHT_STRTAB;
    shdr[2].sh_flags = SHF_ALLOC;
    shdr[2].sh_addr = VDSO_OFF_DYNSTR;
    shdr[2].sh_offset = VDSO_OFF_DYNSTR;
    shdr[2].sh_size = DYNSTR_SIZE;
    shdr[2].sh_addralign = 1;

    shdr[3].sh_name = 0;
    shdr[3].sh_type = SHT_DYNSYM;
    shdr[3].sh_flags = SHF_ALLOC;
    shdr[3].sh_addr = VDSO_OFF_DYNSYM;
    shdr[3].sh_offset = VDSO_OFF_DYNSYM;
    shdr[3].sh_size = (VDSO_NUM_SYMS + 1) * sizeof(elf64_sym_t);
    shdr[3].sh_link = 2;
    shdr[3].sh_info = 1;
    shdr[3].sh_addralign = 8;
    shdr[3].sh_entsize = sizeof(elf64_sym_t);

    shdr[4].sh_name = 0;
    shdr[4].sh_type = SHT_HASH;
    shdr[4].sh_flags = SHF_ALLOC;
    shdr[4].sh_addr = VDSO_OFF_HASH;
    shdr[4].sh_offset = VDSO_OFF_HASH;
    shdr[4].sh_size = HASH_SIZE;
    shdr[4].sh_link = 3;
    shdr[4].sh_addralign = 4;
    shdr[4].sh_entsize = 4;

    shdr[5].sh_name = 0;
    shdr[5].sh_type = SHT_DYNAMIC;
    shdr[5].sh_flags = SHF_ALLOC;
    shdr[5].sh_addr = VDSO_OFF_DYNAMIC;
    shdr[5].sh_offset = VDSO_OFF_DYNAMIC;
    shdr[5].sh_size = 6 * sizeof(elf64_dyn_t);
    shdr[5].sh_link = 2;
    shdr[5].sh_addralign = 8;
    shdr[5].sh_entsize = sizeof(elf64_dyn_t);

    return VDSO_BASE;
}
