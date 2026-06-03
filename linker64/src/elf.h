/* elf.h — minimal ELF64/AArch64 type definitions for muplar-linker64
 * No system headers required.
 */
#pragma once
#include <stdint.h>

#define EI_NIDENT 16
#define ELFMAG0   0x7f
#define ELFMAG1   'E'
#define ELFMAG2   'L'
#define ELFMAG3   'F'
#define ELFCLASS64  2
#define ELFDATA2LSB 1

#define ET_DYN  3
#define EM_AARCH64 183

#define PT_LOAD    1
#define PT_DYNAMIC 2
#define PT_INTERP  3
#define PT_PHDR    6
#define PT_TLS     7
#define PT_GNU_RELRO 0x6474e552

#define DT_NULL      0
#define DT_NEEDED    1
#define DT_PLTRELSZ  2
#define DT_PLTGOT    3
#define DT_STRTAB    5
#define DT_SYMTAB    6
#define DT_RELA      7
#define DT_RELASZ    8
#define DT_RELAENT   9
#define DT_STRSZ    10
#define DT_SONAME   14
#define DT_RPATH    15
#define DT_PLTREL   20
#define DT_JMPREL   23
#define DT_RUNPATH  29
#define DT_RELACOUNT 0x6ffffff9
#define DT_ANDROID_RELA      0x60000011
#define DT_ANDROID_RELASZ    0x60000012

#define R_AARCH64_NONE       0
#define R_AARCH64_ABS64    257
#define R_AARCH64_GLOB_DAT 1025
#define R_AARCH64_JUMP_SLOT 1026
#define R_AARCH64_RELATIVE 1027
#define R_AARCH64_TLS_TPREL64 1030
#define R_AARCH64_TLSDESC     1031
#define R_AARCH64_IRELATIVE   1032

#define STB_LOCAL  0
#define STB_WEAK   2
#define STB_FROM_INFO(i) ((i) >> 4)
#define SHN_UNDEF 0
#define STT_GNU_IFUNC 10
#define ELF64_ST_TYPE(i) ((i) & 0xf)

#define PF_X 1
#define PF_W 2
#define PF_R 4

#define AT_NULL      0
#define AT_PHDR      3
#define AT_PHENT     4
#define AT_PHNUM     5
#define AT_PAGESZ    6
#define AT_BASE      7
#define AT_ENTRY     9
#define AT_HWCAP    16
#define AT_RANDOM   25
#define AT_EXECFN   31

typedef struct {
    uint8_t  e_ident[EI_NIDENT];
    uint16_t e_type, e_machine;
    uint32_t e_version;
    uint64_t e_entry, e_phoff, e_shoff;
    uint32_t e_flags;
    uint16_t e_ehsize, e_phentsize, e_phnum;
    uint16_t e_shentsize, e_shnum, e_shstrndx;
} Elf64_Ehdr;

typedef struct {
    uint32_t p_type, p_flags;
    uint64_t p_offset, p_vaddr, p_paddr;
    uint64_t p_filesz, p_memsz, p_align;
} Elf64_Phdr;

typedef struct {
    int64_t  d_tag;
    uint64_t d_val;
} Elf64_Dyn;

typedef struct {
    uint32_t st_name;
    uint8_t  st_info, st_other;
    uint16_t st_shndx;
    uint64_t st_value, st_size;
} Elf64_Sym;

typedef struct {
    uint64_t r_offset;
    uint64_t r_info;
    int64_t  r_addend;
} Elf64_Rela;

#define ELF64_R_SYM(i)  ((uint32_t)((i) >> 32))
#define ELF64_R_TYPE(i) ((uint32_t)((i) & 0xffffffff))

typedef struct {
    uint64_t a_type;
    uint64_t a_val;
} Elf64_auxv_t;
