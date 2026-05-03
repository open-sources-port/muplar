// compat/elf-shim.h
#pragma once

#ifdef __APPLE__

#include <mach-o/loader.h>
#include <mach-o/nlist.h>
#include <mach-o/fat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <sys_compat/muplar_errno.h>

/* Minimal ELF-like definitions for macOS Mach-O */

typedef struct {
    uint8_t  e_ident[16];   /* mimic ELF magic bytes */
    uint16_t e_type;
    uint16_t e_machine;
    uint32_t e_version;
    uint64_t e_entry;       /* entry point */
    uint64_t e_phoff;       /* program header offset (fake) */
    uint64_t e_shoff;       /* section header offset (fake) */
    uint32_t e_flags;
    uint16_t e_ehsize;
    uint16_t e_phentsize;
    uint16_t e_phnum;       /* number of segments */
    uint16_t e_shentsize;
    uint16_t e_shnum;
    uint16_t e_shstrndx;
} Elf64_Ehdr;

/* Fake Elf64_Phdr for segment info */
typedef struct {
    uint32_t p_type;
    uint32_t p_flags;
    uint64_t p_offset;
    uint64_t p_vaddr;
    uint64_t p_paddr;
    uint64_t p_filesz;
    uint64_t p_memsz;
    uint64_t p_align;
} Elf64_Phdr;

/* Open Mach-O binary and fill fake ELF header */
static inline int elf_open(const char *path, Elf64_Ehdr *ehdr)
{
    int fd = open(path, O_RDONLY);
    if (fd < 0) return -1;

    struct mach_header_64 mh;
    ssize_t r = read(fd, &mh, sizeof(mh));
    if (r != sizeof(mh)) { close(fd); return -1; }

    memset(ehdr, 0, sizeof(*ehdr));
    ehdr->e_entry = 0;          /* Mach-O entry not easily available */
    ehdr->e_phnum = mh.ncmds;   /* number of load commands */
    close(fd);
    return 0;
}

/* Iterate Mach-O segments like ELF program headers */
static inline int elf_get_phdrs(const char *path, Elf64_Phdr *phdrs, uint16_t max)
{
    int fd = open(path, O_RDONLY);
    if (fd < 0) return -1;

    struct mach_header_64 mh;
    if (read(fd, &mh, sizeof(mh)) != sizeof(mh)) { close(fd); return -1; }

    uint32_t n = mh.ncmds;
    struct load_command lc;
    off_t offset = sizeof(mh);

    uint16_t i = 0;
    while (n-- && i < max) {
        lseek(fd, offset, SEEK_SET);
        if (read(fd, &lc, sizeof(lc)) != sizeof(lc)) break;

        if (lc.cmd == LC_SEGMENT_64) {
            struct segment_command_64 sc;
            lseek(fd, offset, SEEK_SET);
            if (read(fd, &sc, sizeof(sc)) != sizeof(sc)) break;

            phdrs[i].p_type   = sc.cmd;
            phdrs[i].p_flags  = sc.initprot;
            phdrs[i].p_offset = sc.fileoff;
            phdrs[i].p_vaddr  = sc.vmaddr;
            phdrs[i].p_paddr  = sc.vmaddr;
            phdrs[i].p_filesz = sc.filesize;
            phdrs[i].p_memsz  = sc.vmsize;
            phdrs[i].p_align  = 0; /* Mach-O does not store alignment like ELF */
            i++;
        }
        offset += lc.cmdsize;
    }

    close(fd);
    return i; /* number of "program headers" filled */
}

#endif /* __APPLE__ */
