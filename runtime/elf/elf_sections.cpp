#include "elf_sections.h"

#include <iostream>

namespace muplar::runtime::elf {

    std::vector<Elf64_Shdr> parse_sections(std::ifstream& file, const Elf64_Ehdr& ehdr) {
        std::vector<Elf64_Shdr> section_headers(ehdr.e_shnum);
        file.seekg(ehdr.e_shoff);
        for (int i = 0; i < ehdr.e_shnum; ++i) {
            file.read( reinterpret_cast<char*>(&section_headers[i]), sizeof(Elf64_Shdr) );
        }

        Elf64_Shdr shstr = section_headers[ehdr.e_shstrndx];
        std::vector<char> shstrtab(shstr.sh_size);
        file.seekg(shstr.sh_offset);
        file.read( shstrtab.data(), shstr.sh_size );

        return section_headers;
    }

    void parse_symbols( std::ifstream& file, const std::vector<Elf64_Shdr>& section_headers ) {
        for (const auto& shdr : section_headers) {

            if (shdr.sh_type != SHT_SYMTAB) {
                continue;
            }

            std::cout << "Symbols:\n";

            Elf64_Shdr strtab_header = section_headers[shdr.sh_link];

            std::vector<char> strtab(strtab_header.sh_size);

            file.seekg(strtab_header.sh_offset);

            file.read( strtab.data(), strtab_header.sh_size );

            uint64_t symbol_count = shdr.sh_size / sizeof(Elf64_Sym);

            file.seekg(shdr.sh_offset);

            for (uint64_t i = 0; i < symbol_count; ++i) {

                Elf64_Sym symbol {};

                file.read( reinterpret_cast<char*>(&symbol), sizeof(Elf64_Sym) );

                if (symbol.st_name == 0) {
                    continue;
                }

                const char* symbol_name = strtab.data() + symbol.st_name;

                uint8_t type = ELF64_ST_TYPE(symbol.st_info);
                if (type != STT_FUNC) {
                    continue;
                }

                std::cout << "  " << symbol_name << "\n";
                std::cout << "    value: 0x" << std::hex << symbol.st_value << "\n";
                std::cout << "    size : 0x" << std::hex << symbol.st_size << "\n\n";
            }
        }
    }

    void print_sections( std::ifstream& file, const Elf64_Ehdr& ehdr, const std::vector<Elf64_Shdr>& section_headers ) {
        Elf64_Shdr shstr = section_headers[ehdr.e_shstrndx];

        std::vector<char> shstrtab(shstr.sh_size);

        file.seekg(shstr.sh_offset);

        file.read( shstrtab.data(), shstr.sh_size );

        std::cout << "Sections:\n";

        for (const auto& shdr : section_headers) {

            if (shdr.sh_name == 0) {
                continue;
            }
            const char* name = shstrtab.data() + shdr.sh_name;

            std::cout << "  " << name << "\n";
            std::cout << "    addr  : 0x" << std::hex << shdr.sh_addr << "\n";
            std::cout << "    offset: 0x" << std::hex << shdr.sh_offset << "\n";
            std::cout << "    size  : 0x" << std::hex << shdr.sh_size << "\n\n";
        }
    }
}