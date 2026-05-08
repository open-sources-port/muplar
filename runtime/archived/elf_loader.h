#pragma once

#include "elf_types.h"

namespace muplar::runtime::elf {

    ElfBinary parse(const char* path);

}