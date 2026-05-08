#pragma once

#include <elf.h>
#include <fstream>
#include <vector>

#include "elf_loader.h"
#include "virtual_memory.h"

namespace muplar::runtime::elf {

    std::vector<Elf64_Shdr> parse_sections( std::ifstream& file, const Elf64_Ehdr& ehdr);

    void parse_symbols( std::ifstream& file, const std::vector<Elf64_Shdr>& section_headers );

    void print_sections( std::ifstream& file, const Elf64_Ehdr& ehdr, const std::vector<Elf64_Shdr>& section_headers );
}
