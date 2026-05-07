#pragma once

#include <fstream>
#include <elf.h>

namespace muplar::runtime::elf {

    bool validate_elf( std::ifstream& file, Elf64_Ehdr& ehdr );

}
