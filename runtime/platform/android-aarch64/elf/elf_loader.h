#pragma once

#include <string>
#include "elf_image.h"

namespace muplar::runtime::elf {

    class ElfLoader {
        public:
            ElfImage load(const std::string& path);
    };

}
