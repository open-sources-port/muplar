#include "virtual_memory.h"

#include <cstring>
#include <iomanip>
#include <iostream>

namespace muplar::runtime::memory {

    void VirtualMemory::map(
        uint64_t vaddr,
        uint64_t size,
        uint32_t protection,
        const uint8_t* src,
        uint64_t filesz
    ) {
        MappedSegment segment {};

        segment.vaddr = vaddr;
        segment.size = size;
        segment.protection = protection;

        segment.data.resize(size);

        if (src && filesz > 0) {
            std::memcpy(segment.data.data(), src, filesz);
        }

        segments.push_back(std::move(segment));
    }

    void VirtualMemory::dump() const {
        std::cout << "\nVirtual Memory Layout:\n";

        for (const auto& s : segments) {
            std::cout
                << "  vaddr=0x"
                << std::hex
                << s.vaddr
                << " size=0x"
                << s.size
                << " prot=";

            if (s.protection & READ) {
                std::cout << "R";
            }

            if (s.protection & WRITE) {
                std::cout << "W";
            }

            if (s.protection & EXECUTE) {
                std::cout << "X";
            }

            std::cout << "\n";
        }

        std::cout << "\n";
    }

    const uint8_t* VirtualMemory::translate(uint64_t vaddr) const {
        for (const auto& s : segments) {

            uint64_t start = s.vaddr;
            uint64_t end = s.vaddr + s.size;

            if (vaddr >= start && vaddr < end) {

                uint64_t offset = vaddr - start;

                return s.data.data() + offset;
            }
        }

        return nullptr;
    }
}
