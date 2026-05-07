#include "elf_program_headers.h"
#include <iostream>

namespace muplar::runtime::elf {

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

    void parse_program_headers( std::ifstream& file, const Elf64_Ehdr& ehdr, ElfBinary& binary) {
        file.seekg(ehdr.e_phoff);

        std::cout << "ELF64\n";
        std::cout << "Machine: AArch64\n";
        std::cout << "Endian : Little\n\n";

        std::cout << "Entrypoint: 0x" << std::hex << binary.entrypoint << "\n\n";

        std::cout << "Program Headers:\n";

        for (int i = 0; i < ehdr.e_phnum; ++i) {
            Elf64_Phdr phdr {};

            file.read(
                reinterpret_cast<char*>(&phdr),
                sizeof(Elf64_Phdr)
            );

            std::cout << "  " << phdr_type_name(phdr.p_type) << "\n";
            std::cout << "    offset: 0x" << std::hex << phdr.p_offset << "\n";
            std::cout << "    vaddr : 0x" << std::hex << phdr.p_vaddr << "\n";
            std::cout << "    filesz: 0x" << std::hex << phdr.p_filesz << "\n";
            std::cout << "    memsz : 0x" << std::hex << phdr.p_memsz << "\n";
            std::cout << "    align : 0x" << std::hex << phdr.p_align << "\n";
            std::cout << "    flags : ";
            print_flags(phdr.p_flags);
            std::cout << "\n\n";

            std::streampos phdr_position = file.tellg();

            if (phdr.p_type == PT_LOAD) {
                std::vector<uint8_t> buffer(phdr.p_filesz);

                file.seekg(phdr.p_offset);

                file.read( reinterpret_cast<char*>(buffer.data()), phdr.p_filesz );

                uint32_t protection = 0;

                if (phdr.p_flags & PF_R) {
                    protection |= memory::READ;
                }

                if (phdr.p_flags & PF_W) {
                    protection |= memory::WRITE;
                }

                if (phdr.p_flags & PF_X) {
                    protection |= memory::EXECUTE;
                }

                binary.vm.map(
                    phdr.p_vaddr,
                    phdr.p_memsz,
                    protection,
                    buffer.data(),
                    phdr.p_filesz
                );

                MemorySegment segment {};

                segment.offset = phdr.p_offset;
                segment.vaddr = phdr.p_vaddr;
                segment.filesz = phdr.p_filesz;
                segment.memsz = phdr.p_memsz;
                segment.align = phdr.p_align;
                segment.flags = phdr.p_flags;

                binary.segments.push_back(segment);

                file.seekg(phdr_position);
            }
        }
    }

}