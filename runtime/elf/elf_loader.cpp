#include "elf_loader.h"

#include <elf.h>
#include <fstream>
#include <iostream>
#include <vector>

namespace muplar::runtime::elf
{
    static const char* phdr_type_name(uint32_t type) {
        switch (type) {
            case PT_LOAD:         return "LOAD";
            case PT_DYNAMIC:      return "DYNAMIC";
            case PT_INTERP:       return "INTERP";
            case PT_PHDR:         return "PHDR";

            case PT_GNU_STACK:    return "GNU_STACK";
            case PT_GNU_RELRO:    return "GNU_RELRO";
            case PT_GNU_EH_FRAME: return "GNU_EH_FRAME";
            case PT_NOTE:         return "NOTE";
            default:              return "UNKNOWN";
        }
    }

    static void print_flags(uint32_t flags) {
        if (flags & PF_R) std::cout << "R";
        if (flags & PF_W) std::cout << "W";
        if (flags & PF_X) std::cout << "X";
    }

    ElfBinary parse(const char* path)
    {
        ElfBinary binary {};
        std::ifstream file(path, std::ios::binary);

        if (!file) {
            std::cerr << "Failed to open ELF file\n";
            return binary;
        }

        Elf64_Ehdr ehdr {};

        file.read(
            reinterpret_cast<char*>(&ehdr),
            sizeof(Elf64_Ehdr)
        );

        if (!file) {
            std::cerr << "Failed to read ELF header\n";
            return binary;
        }

        if (
            ehdr.e_ident[EI_MAG0] != ELFMAG0 ||
            ehdr.e_ident[EI_MAG1] != ELFMAG1 ||
            ehdr.e_ident[EI_MAG2] != ELFMAG2 ||
            ehdr.e_ident[EI_MAG3] != ELFMAG3
        ) {
            std::cerr << "Invalid ELF magic\n";
            return binary;
        }

        if (ehdr.e_ident[EI_CLASS] != ELFCLASS64) {
            std::cerr << "Unsupported ELF class\n";
            return binary;
        }

        if (ehdr.e_ident[EI_DATA] != ELFDATA2LSB) {
            std::cerr << "Unsupported ELF endianness\n";
            return binary;
        }

        if (ehdr.e_machine != EM_AARCH64) {
            std::cerr << "Unsupported machine type\n";
            return binary;
        }

        binary.valid = true;
        binary.entrypoint = ehdr.e_entry;

        file.seekg(ehdr.e_phoff);

        std::cout << "ELF64\n";
        std::cout << "Machine: AArch64\n";
        std::cout << "Endian : Little\n\n";

        std::cout
            << "Entrypoint: 0x"
            << std::hex
            << binary.entrypoint
            << "\n\n";

        std::cout << "Program Headers:\n";

        for (int i = 0; i < ehdr.e_phnum; ++i) {
            Elf64_Phdr phdr {};

            file.read(
                reinterpret_cast<char*>(&phdr),
                sizeof(Elf64_Phdr)
            );

            std::cout
                << "  "
                << phdr_type_name(phdr.p_type)
                << "\n";

            std::cout
                << "    offset: 0x"
                << std::hex
                << phdr.p_offset
                << "\n";

            std::cout
                << "    vaddr : 0x"
                << std::hex
                << phdr.p_vaddr
                << "\n";

            std::cout
                << "    filesz: 0x"
                << std::hex
                << phdr.p_filesz
                << "\n";

            std::cout
                << "    memsz : 0x"
                << std::hex
                << phdr.p_memsz
                << "\n";

            std::cout
                << "    align : 0x"
                << std::hex
                << phdr.p_align
                << "\n";

            std::cout << "    flags : ";

            print_flags(phdr.p_flags);

            std::cout << "\n\n";

            if (phdr.p_type == PT_LOAD) {
                MemorySegment segment {};

                segment.offset = phdr.p_offset;
                segment.vaddr = phdr.p_vaddr;
                segment.filesz = phdr.p_filesz;
                segment.memsz = phdr.p_memsz;
                segment.align = phdr.p_align;
                segment.flags = phdr.p_flags;

                binary.segments.push_back(segment);
            }
        }

        return binary;

    }
}
