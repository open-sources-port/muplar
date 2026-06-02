#pragma once

#include <cstdint>
#include <vector>

namespace muplar::runtime::elf {

    struct Segment {
        uint64_t vaddr  = 0;
        uint64_t memsz  = 0;
        uint64_t filesz = 0;
        uint64_t offset = 0;

        uint32_t flags  = 0;
    };

    struct ElfImage {
        uint64_t entry = 0;

        uint16_t type    = 0;
        uint16_t machine = 0;

        uint64_t load_min = 0;
        uint64_t load_max = 0;

        std::vector<Segment> segments;
        std::vector<uint8_t> raw;
    };

}
