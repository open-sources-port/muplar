#include "elf_loader.h"
#include "elf_validator.h"
#include "elf_program_headers.h"
#include "elf_sections.h"

#include <elf.h>
#include <fstream>
#include <iostream>

namespace muplar::runtime::elf
{

    ElfBinary parse(const char* path)
    {
        ElfBinary binary {};
        std::ifstream file(path, std::ios::binary);

        if (!file) {
            std::cerr << "Failed to open ELF file\n";
            return binary;
        }

        Elf64_Ehdr ehdr {};

        if (!validate_elf(file, ehdr)) {
            return binary;
        }

        binary.valid = true;
        binary.entrypoint = ehdr.e_entry;

        parse_program_headers(file, ehdr, binary);
        binary.vm.dump();

        const uint8_t* entry = binary.vm.translate(binary.entrypoint);

        if (entry) {
            std::cout << "Entrypoint host pointer: " << static_cast<const void*>(entry) << "\n";
        } else {
            std::cout << "Entrypoint translation failed\n";
        }

        auto section_headers = parse_sections(file, ehdr);
        print_sections( file, ehdr, section_headers );
        parse_symbols( file, section_headers );


        return binary;

    }
}
