#include "elf_loader.h"

#include <elf.h>
#include <fstream>
#include <iostream>

namespace muplar::runtime::elf
{
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

        std::cout << "ELF detected\n";
        std::cout << "Entrypoint: 0x"
                  << std::hex
                  << ehdr.e_entry
                  << std::endl;

        return true;
    }
}
