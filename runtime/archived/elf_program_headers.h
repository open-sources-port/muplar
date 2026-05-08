#pragma once

#include <elf.h>
#include <fstream>

#include "elf_loader.h"
#include "virtual_memory.h"

namespace muplar::runtime::elf {

    void parse_program_headers( std::ifstream& file, const Elf64_Ehdr& ehdr, ElfBinary& binary);

}
