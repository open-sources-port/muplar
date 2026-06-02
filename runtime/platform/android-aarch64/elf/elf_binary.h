// runtime/platform/android-aarch64/elf/elf_binary.h

#pragma once

#include "elf_image.h"

#include "virtual_memory.h"

namespace muplar::runtime::elf {

    struct MemorySegment {
        uint64_t guest = 0;
        uint64_t size  = 0;

        uint32_t prot = 0;
    };

    struct ElfBinary {
        bool valid = false;

        uint64_t entrypoint = 0;

        memory::VirtualMemory vm;

        std::vector<MemorySegment> segments;
    };

}
