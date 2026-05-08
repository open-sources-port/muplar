// runtime/runtime_builder.h

#pragma once

#include "elf/elf_binary.h"
#include "elf/elf_image.h"

namespace muplar::runtime {

    elf::ElfBinary build_binary(
        const elf::ElfImage& image
    );

}
