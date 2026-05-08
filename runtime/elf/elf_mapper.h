#pragma once

#include "elf_image.h"
#include "mapped_elf_image.h"

namespace muplar::runtime::elf {

class ElfMapper {
    public:
        MappedElfImage map(const ElfImage& image, const char* file_path);
    };
}
