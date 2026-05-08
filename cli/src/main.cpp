#include <iostream>

#include "elf_loader.h"
#include "runtime_builder.h"

int main(int argc, char** argv) {
    std::cout << "Muplar CLI (mup)\n";

    if (argc < 2) {
        std::cerr << "Usage: mup <elf-file>\n";
        return 1;
    }

    try {
        muplar::runtime::elf::ElfLoader loader;

        auto image = loader.load(argv[1]);

        auto binary =
            muplar::runtime::build_binary(image);

        std::cout << "Entry : 0x" << std::hex << binary.entrypoint << "\n";
        void* entry = binary.vm.translate_host( binary.entrypoint );
        std::cout << "Entry host : " << entry << "\n";

        binary.vm.dump();

        return 0;

    } catch (const std::exception& e) {

        std::cerr << "Error: "
                  << e.what()
                  << "\n";

        return 1;
    }
}