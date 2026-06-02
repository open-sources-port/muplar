#pragma once

#include <cstdint>

#include "mapped_elf_image.h"

namespace muplar::runtime::elf {

    class ExecutionContext {
        public:
            int execute(const MappedElfImage& image);
    };

}