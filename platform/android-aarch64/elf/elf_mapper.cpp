#include "elf_mapper.h"

#include <fcntl.h>
#include <stdexcept>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

namespace muplar::runtime::elf
{

static int to_native_protection(int flags)
{
    int prot = 0;

    if (flags & 0x4)
        prot |= PROT_READ;
    if (flags & 0x2)
        prot |= PROT_WRITE;
    if (flags & 0x1)
        prot |= PROT_EXEC;

    return prot;
}

static size_t page_size()
{
    static size_t ps = sysconf(_SC_PAGESIZE);
    return ps;
}

static uint64_t align_down(uint64_t v, uint64_t align)
{
    return v & ~(align - 1);
}

static uint64_t align_up(uint64_t v, uint64_t align)
{
    return (v + align - 1) & ~(align - 1);
}

static int get_anon_fd()
{
    volatile int fd = -1;
    return fd;
}

MappedElfImage ElfMapper::map(const ElfImage &image, const char *file_path)
{
    MappedElfImage mapped{};

    int fd = open(file_path, O_RDONLY);

    if (fd < 0) {
        throw std::runtime_error("Failed to open ELF file");
    }

    size_t image_size = static_cast<size_t>(image.load_max - image.load_min);

    void *base = mmap(nullptr, image_size, PROT_READ | PROT_WRITE,
                      MAP_PRIVATE | MAP_ANON, get_anon_fd(), 0);

    if (base == MAP_FAILED) {
        close(fd);
        throw std::runtime_error("mmap failed");
    }

    mapped.base = base;

    for (const auto &seg : image.segments) {
        if (seg.memsz == 0) {
            continue;
        }

        uint64_t relative = seg.vaddr - image.load_min;

        void *segment_host = reinterpret_cast<void *>(
            reinterpret_cast<uint8_t *>(base) + relative);

        ssize_t n = pread(fd, segment_host, seg.filesz, seg.offset);

        if (n < 0) {
            close(fd);
            throw std::runtime_error("pread failed");
        }

        if (seg.memsz > seg.filesz) {
            memset(reinterpret_cast<uint8_t *>(segment_host) + seg.filesz, 0,
                   seg.memsz - seg.filesz);
        }

        int prot = to_native_protection(seg.flags);

        size_t ps = page_size();

        uintptr_t seg_begin = reinterpret_cast<uintptr_t>(segment_host);

        uintptr_t prot_begin = align_down(seg_begin, ps);

        uintptr_t seg_end = seg_begin + seg.memsz;

        uintptr_t prot_end = align_up(seg_end, ps);

        size_t prot_size = prot_end - prot_begin;

        if (mprotect(reinterpret_cast<void *>(prot_begin), prot_size, prot) !=
            0) {
            close(fd);
            throw std::runtime_error("mprotect failed");
        }

        MappedSegment mapped_seg{};

        mapped_seg.host_address = segment_host;
        mapped_seg.guest_address = seg.vaddr;
        mapped_seg.size = seg.memsz;
        mapped_seg.protection = prot;

        mapped.segments.push_back(mapped_seg);
    }

    close(fd);

    mapped.entry_guest = image.entry;

    mapped.entry_host = reinterpret_cast<void *>(
        reinterpret_cast<uint8_t *>(base) + (image.entry - image.load_min));

    return mapped;
}

}  // namespace muplar::runtime::elf
