// platform/commons/memory/virtual_memory.h

#pragma once

#include <cstdint>
#include <vector>

namespace muplar::runtime::memory {

    enum PageProtection : uint32_t {
        READ    = 1 << 0,
        WRITE   = 1 << 1,
        EXECUTE = 1 << 2
    };

    struct MappedSegment {
        uint64_t vaddr = 0;
        uint64_t size  = 0;

        uint32_t protection = 0;

        std::vector<uint8_t> data;

        void* host = nullptr;
    };

    class VirtualMemory {

    public:

        void map(
            uint64_t vaddr,
            uint64_t size,
            uint32_t protection,
            const uint8_t* src,
            uint64_t filesz
        );

        void dump() const;

        const uint8_t* translate(uint64_t vaddr) const;

        uint8_t* translate_mut(uint64_t vaddr);

        void* translate_host(uint64_t vaddr) const;

    private:

        std::vector<MappedSegment> segments;
    };

}
