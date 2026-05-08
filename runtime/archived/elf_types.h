#pragma once

#include <cstdint>
#include <vector>
#include "virtual_memory.h"

namespace muplar::runtime::elf {

    struct MemorySegment {
        uint64_t offset;
        uint64_t vaddr;
        uint64_t filesz;
        uint64_t memsz;
        uint64_t align;
        uint32_t flags;
    };

    struct ElfBinary {
        bool valid = false;

        uint64_t entrypoint = 0;

        memory::VirtualMemory vm;
        std::vector<MemorySegment> segments;

    };

}
