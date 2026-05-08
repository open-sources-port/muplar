#include "elf_dumper.h"

#include <iostream>

namespace muplar::runtime::elf {

    static void print_flags(uint32_t flags)
    {
        if (flags & PF_R) std::cout << "R";
        if (flags & PF_W) std::cout << "W";
        if (flags & PF_X) std::cout << "X";
    }

    void ElfDumper::dump(const ElfBinary& binary)
    {
        std::cout << "Entrypoint: 0x" << std::hex << binary.entrypoint << "\n\n";

        std::cout << "Segments:\n";
        for (const auto& segment : binary.segments) {
            std::cout << "  offset : 0x" << std::hex << segment.offset << "\n";
            std::cout << "  vaddr : 0x" << std::hex << segment.vaddr << "\n";
            std::cout << "  filesz: 0x" << std::hex << segment.filesz << "\n";
            std::cout << "  memsz : 0x" << std::hex << segment.memsz << "\n";
            std::cout << "  align : 0x" << std::hex << segment.align << "\n";

            std::cout << "  flags : ";
            print_flags(segment.flags);
            std::cout << "\n\n";
        }
    }

}
