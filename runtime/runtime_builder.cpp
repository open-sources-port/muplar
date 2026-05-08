// runtime/runtime_builder.cpp

#include "runtime_builder.h"

#include <elf.h>

namespace muplar::runtime {

    using namespace elf;
    using namespace memory;

    ElfBinary build_binary(
        const ElfImage& image
    ) {
        ElfBinary binary {};

        binary.valid = true;
        binary.entrypoint = image.entry;

        for (const auto& seg : image.segments) {

            uint32_t prot = 0;

            if (seg.flags & PF_R) {
                prot |= READ;
            }

            if (seg.flags & PF_W) {
                prot |= WRITE;
            }

            if (seg.flags & PF_X) {
                prot |= EXECUTE;
            }

            binary.vm.map(
                seg.vaddr,
                seg.memsz,
                prot,
                image.raw.data() + seg.offset,
                seg.filesz
            );

            elf::MemorySegment ms {};

            ms.guest = seg.vaddr;
            ms.size  = seg.memsz;
            ms.prot  = prot;

            binary.segments.push_back(ms);
        }

        return binary;
    }

}
