#pragma once

#include <cstdint>
#include <vector>

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

        std::vector<MemorySegment> segments;

    };

}
