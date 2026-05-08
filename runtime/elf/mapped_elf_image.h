#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace muplar::runtime::elf {

    struct MappedSegment {
        void* host_address = nullptr;

        uint64_t guest_address = 0;

        size_t size = 0;

        int protection = 0;
    };

    struct MappedElfImage {
        void* base = nullptr;

        uint64_t entry_guest = 0;
        void* entry_host = nullptr;

        std::vector<MappedSegment> segments;
    };

}
