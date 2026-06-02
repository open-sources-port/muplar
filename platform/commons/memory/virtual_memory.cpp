// platform/commons/memory/virtual_memory.cpp

#include "virtual_memory.h"

#include <cstring>
#include <iomanip>
#include <iostream>
#include <sys/mman.h>
#include <stdexcept>

namespace muplar::runtime::memory {

    static int to_native_prot(uint32_t prot) {
        int native = 0;

        if (prot & READ) {
            native |= PROT_READ;
        }

        if (prot & WRITE) {
            native |= PROT_WRITE;
        }

        if (prot & EXECUTE) {
            native |= PROT_EXEC;
        }

        return native;
    }

    void VirtualMemory::map(
        uint64_t vaddr,
        uint64_t size,
        uint32_t protection,
        const uint8_t* src,
        uint64_t filesz
    ) {
        MappedSegment seg {};

        seg.vaddr = vaddr;
        seg.size  = size;

        seg.protection = protection;

        seg.data.resize(size);

        seg.host = mmap( nullptr, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0 );

        if (seg.host == MAP_FAILED) {
            throw std::runtime_error( "Failed to mmap executable memory" );
        }

        if (src && filesz > 0) {
            memcpy(seg.data.data(), src, filesz);
        }

        if (src && filesz > 0) {
            memcpy( seg.host, src, filesz );
        }

        if (size > filesz) {
            memset( (uint8_t*)seg.host + filesz, 0, size - filesz );
        }

        if (size > filesz) {
            memset(seg.data.data() + filesz, 0, size - filesz);
        }

        int native_prot =
        to_native_prot(protection);
        mprotect( seg.host, size, native_prot );

        segments.push_back(std::move(seg));
    }

    void VirtualMemory::dump() const {
        std::cout << "\n=== Virtual Memory Map ===\n";

        for (const auto& seg : segments) {
            std::cout
                << "segment\n"
                << "  vaddr : 0x" << std::hex << seg.vaddr << "\n"
                << "  host  : " << seg.host << "\n"
                << "  size  : 0x" << seg.size << "\n"
                << "  prot  : "
                << ((seg.protection & READ) ? "R" : "-")
                << ((seg.protection & WRITE) ? "W" : "-")
                << ((seg.protection & EXECUTE) ? "X" : "-")
                << "\n\n";
        }
    }

    const uint8_t* VirtualMemory::translate(uint64_t vaddr) const {
        for (const auto& seg : segments) {

            if (vaddr < seg.vaddr) {
                continue;
            }

            if (vaddr >= seg.vaddr + seg.size) {
                continue;
            }

            uint64_t offset = vaddr - seg.vaddr;

            return seg.data.data() + offset;
        }

        return nullptr;
    }

    uint8_t* VirtualMemory::translate_mut(uint64_t vaddr) {
        for (auto& seg : segments) {

            if (vaddr < seg.vaddr) {
                continue;
            }

            if (vaddr >= seg.vaddr + seg.size) {
                continue;
            }

            uint64_t offset = vaddr - seg.vaddr;

            return seg.data.data() + offset;
        }

        return nullptr;
    }

    void* VirtualMemory::translate_host(
        uint64_t vaddr
    ) const {

        for (const auto& seg : segments) {

            if (vaddr < seg.vaddr) {
                continue;
            }

            if (vaddr >= seg.vaddr + seg.size) {
                continue;
            }

            uint64_t offset = vaddr - seg.vaddr;

            return (uint8_t*)seg.host + offset;
        }

        return nullptr;
    }
}
