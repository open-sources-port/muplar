#include "elf_loader.h"

#include <stdexcept>
#include <fstream>
#include "virtual_memory.h"

// elfuse headers
extern "C" {
#include "core/elf.h"
}

namespace muplar::runtime::elf
{

ElfImage ElfLoader::load(const std::string &path)
{
    elf_info_t info{};

    int rc = elf_load(path.c_str(), &info);

    if (rc != 0) {
        throw std::runtime_error("Failed to load ELF");
    }

    ElfImage image{};

    image.entry = info.entry;
    image.type = info.e_type;
    image.machine = info.e_machine;

    image.load_min = info.load_min;
    image.load_max = info.load_max;

    for (int i = 0; i < info.num_segments; ++i) {
        const auto &s = info.segments[i];

        Segment seg{};

        seg.vaddr = s.gpa;
        seg.memsz = s.memsz;
        seg.filesz = s.filesz;
        seg.offset = s.offset;
        seg.flags = s.flags;

        image.segments.push_back(seg);
    }

    std::ifstream file(path, std::ios::binary);

    file.seekg(0, std::ios::end);

    size_t size = static_cast<size_t>(file.tellg());

    file.seekg(0);

    image.raw.resize(size);

    file.read(reinterpret_cast<char *>(image.raw.data()), size);

    return image;
}

}  // namespace muplar::runtime::elf
