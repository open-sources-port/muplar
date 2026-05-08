#include "elf_validator.h"

#include <iostream>

namespace muplar::runtime::elf {

    bool validate_elf( std::ifstream& file, Elf64_Ehdr& ehdr ) {
        file.read( reinterpret_cast<char*>(&ehdr), sizeof(Elf64_Ehdr) );

        if (!file) {
            std::cerr << "Failed to read ELF header\n";
            return false;
        }

        if (
            ehdr.e_ident[EI_MAG0] != ELFMAG0 ||
            ehdr.e_ident[EI_MAG1] != ELFMAG1 ||
            ehdr.e_ident[EI_MAG2] != ELFMAG2 ||
            ehdr.e_ident[EI_MAG3] != ELFMAG3
        ) {
            std::cerr << "Invalid ELF magic\n";
            return false;
        }

        if (ehdr.e_ident[EI_CLASS] != ELFCLASS64) {
            std::cerr << "Unsupported ELF class\n";
            return false;
        }

        if (ehdr.e_ident[EI_DATA] != ELFDATA2LSB) {
            std::cerr << "Unsupported ELF endianness\n";
            return false;
        }

        if (ehdr.e_machine != EM_AARCH64) {
            std::cerr << "Unsupported machine type\n";
            return false;
        }

        return true;
    }

}
