#include <iostream>
#include <elf_loader.h>

int main(int argc, char** argv) {
    std::cout << "Muplar CLI (mup)\n";

    if (argc < 2) { 
        std::cerr << "Usage: mup <elf-file>\n";
        return 1; 
    } 
    auto binary = muplar::runtime::elf::parse(argv[1]);
    if (!binary.valid) {
        return 1;
    }

    return 0;
}
