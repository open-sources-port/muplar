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

    bool parse(const char* path)
    {
        std::ifstream file(path, std::ios::binary);

        if (!file)
        {
            return false;
        }

        Elf64_Ehdr ehdr{};

        file.read(reinterpret_cast<char*>(&ehdr), sizeof(ehdr));

        if (ehdr.e_ident[EI_MAG0] != ELFMAG0)
        {
            return false;
        }

        std::vector<Elf64_Phdr> phdrs;

        file.seekg(ehdr.e_phoff);

        for (int i = 0; i < ehdr.e_phnum; ++i) {
            Elf64_Phdr phdr {};

            file.read(
                reinterpret_cast<char*>(&phdr),
                sizeof(Elf64_Phdr)
            );

            phdrs.push_back(phdr);
        }

        std::cout << "\nProgram Headers:\n";

        for (const auto& phdr : phdrs) {
            std::cout
                << "  "
                << phdr_type_name(phdr.p_type)
                << "\n";

            std::cout
                << "    vaddr: 0x"
                << std::hex
                << phdr.p_vaddr
                << "\n";

            std::cout
                << "    memsz: 0x"
                << std::hex
                << phdr.p_memsz
                << "\n";

            std::cout
                << "    flags: ";

            if (phdr.p_flags & PF_R) std::cout << "R";
            if (phdr.p_flags & PF_W) std::cout << "W";
            if (phdr.p_flags & PF_X) std::cout << "X";

            std::cout << "\n\n";
        }
        return true;
    }
}
