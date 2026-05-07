#pragma once

#include <elf.h>
#include "elf_loader.h"

namespace muplar::runtime::elf {

    class ElfDumper {
    public:
        static void dump(const ElfBinary& binary);
    };

}
